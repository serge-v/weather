/**
 * Serge Voilokov, 2015.
 * Get forecast from NOAA for specified zip code.
 */

#include "../common/net.h"
#include "../common/xml.h"
#include "../common/struct.h"
#include "version.h"
#include <err.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <curl/curl.h>

#define _XOPEN_SOURCE
#include <time.h>

static bool debug = false;
static const char *mail_recipients = false; /* send mail to comma delimited recipients */
static bool html = false;                   /* output in html format */
static int zip = 10010;                     /* get weather forecast for zip code */
static char fname[PATH_MAX];                /* path to current dwml file */

static struct option longopts[] = {
	{ "zip",          required_argument, NULL, 'z' },
	{ "mail",         required_argument, NULL, 'm' },
	{ "html",         no_argument,       NULL, 't' },
	{ "file",         required_argument, NULL, 'f' },
	{ "debug",        no_argument,       NULL, 'd' },
	{ "help",         no_argument,       NULL, 'h' },
	{ "version",      no_argument,       NULL, 'v' },
	{ NULL,           0,                 NULL,  0  }
};

static void
synopsis()
{
	printf("usage: weather [-dmthv] [-z zip] [-m \"email1,email2,..\"]\n");
}

static void
usage()
{
	synopsis();

	printf(
	       "options:\n"
	       "    -z, --zip=zipcode      get weather forecast for zipcode\n"
	       "    -m, --mail=recipients  send email to recipients\n"
	       "    -t, --html             output in html format\n"
	       "    -d, --debug            output debug information\n"
	       "    -f, --file             input dwml file for debugging\n"
	       "    -v, --version          print version\n"
	       );
}

/* ===== data structures ===================== */

struct time_interval
{
	time_t start_valid_time;
	time_t end_valid_time;
};

struct time_layout
{
	const char *key;    /* example k-p24h-n7-1, k-p3h-n37-3 */
	int period;         /* total time period of layout */
	int seq_number;     /* sequence number of layout */
	int count;          /* number of intervals */
	struct time_interval *intervals;
};

/* forecast display row */

enum temperature_type
{
	TEMPERATURE_HOURLY,
	TEMPERATURE_MAXIMUM,
	TEMPERATURE_MINIMUM,
	TEMPERATURE_APPARENT,
	TEMPERATURE_ENUM_MAX
};

const char *temperature_type_names[] = {
	[TEMPERATURE_HOURLY] = "hourly",
	[TEMPERATURE_MAXIMUM] = "maximum",
	[TEMPERATURE_MINIMUM] = "minimum",
	[TEMPERATURE_APPARENT] = "apparent"
};

struct temperature
{
	int celcius;
	bool has_value;
};

struct humidity
{
	int percent;
	bool has_value;
};

struct wind_speed
{
	int mps;
	bool has_value;
};

struct wind_direction
{
	int degrees;
	bool has_value;
};

struct cloud_amount
{
	int percent;
	bool has_value;
};

struct snow_amount
{
	int centimeters;
	bool has_value;
};

struct row
{
	time_t time;                         /* rounded to the start of the hour */
	struct temperature temp_hourly;
	struct temperature temp_max;
	struct temperature temp_min;
	struct temperature temp_apparent;
	struct humidity humidity;
	struct wind_speed wind_speed;
	struct wind_direction wind_dir;
	struct cloud_amount cloud_amount;
	struct snow_amount snow_amount;
	char *weather;
};

enum legend_position {
	LEGEND_TOP,   /* useful for terminals */
	LEGEND_BOTTOM /* useful for emails */
};

#define MAX_HOURS 24*7

struct dwml
{
	time_t creation_date;               /* NOAA response generating time */
	time_t base_time;                   /* time from which rows data are calculated */
	time_t refresh_frequency;           /* period from creation_date when next fetch makes sence */
	size_t n_layouts;                   /* number of time layouts */
	struct time_layout **time_layouts;  /* array of pointers to time layouts */
	struct row table[MAX_HOURS];        /* table with rows from current hour to +MAX_HOURS */
};

static enum temperature_type
get_temp_type(const char *name)
{
	size_t i;

	for (i = 0; i < TEMPERATURE_ENUM_MAX; i++)
		if (strcmp(name, temperature_type_names[i]) == 0)
			return i;

	return TEMPERATURE_ENUM_MAX;
}

static void
isoize_time(char *timestr, const char *dwml_time)
{
	strncpy(timestr, dwml_time, 25);
	timestr[22] = timestr[23];
	timestr[23] = timestr[24];
	timestr[24] = 0;
}

static struct time_layout *
parse_time_layout(const xmlNodePtr layout_node)
{
	xmlNodePtr n = NULL;
	int res = 0;
	int i = 0;
	struct tm tm;
	char timestr[26];

	n = first_el(layout_node, "layout-key");
	if (n == NULL)
		err(1, "no layout key in time-layout");

	struct time_layout *tl = malloc(sizeof(struct time_layout));

	tl->key = get_ctext(n);
	res = sscanf(tl->key, "k-p%dh-n%d-%d", &tl->period, &tl->count, &tl->seq_number);
	if (res != 3)
		err(1, "cannot parse layout key: %s", tl->key);

	tl->intervals = calloc(tl->count, sizeof(struct time_interval));

	for (i = 0, n = first_el(layout_node, "start-valid-time"); n != NULL; n = next_el(n), i++) {
		const char *text = get_ctext(n);
		/* remove semicolon, i.e. 2015-08-21T08:00:00-04:00 to 2015-08-21T08:00:00-0400 */
		isoize_time(timestr, text);
		/* parse: 2015-08-21T08:00:00-0400 */
		if (strptime(timestr, "%Y-%m-%dT%H:%M:%S%z", &tm) == NULL)
			err(1, "cannot parse start-valid-time: %s", text);
		tl->intervals[i].start_valid_time = mktime(&tm);
	}

	for (i = 0, n = first_el(layout_node, "end-valid-time"); n != NULL; n = next_el(n), i++) {
		const char *text = get_ctext(n);
		isoize_time(timestr, text);
		if (strptime(timestr, "%Y-%m-%dT%H:%M:%S%z", &tm) == NULL)
			err(1, "cannot parse end-valid-time: %s", text);
		tl->intervals[i].end_valid_time = mktime(&tm);
	}

	return tl;
}

static const struct time_layout *
find_layout(const struct dwml *dwml, xmlNodePtr node)
{
	size_t i;
	const char *layout_name = get_attr(node, "time-layout");

	for (i = 0; i < dwml->n_layouts; i++)
		if (strcmp(dwml->time_layouts[i]->key, layout_name) == 0)
			return dwml->time_layouts[i];

	return NULL;
}

static void
parse_temperature(struct dwml* dwml, const xmlNodePtr node)
{
	size_t i;
	xmlNodePtr vn;
	int row_idx;
	const char *value;

	const char *type = get_attr(node, "type");
	enum temperature_type etemp = get_temp_type(type);
	const struct time_layout * layout = find_layout(dwml, node);

	for (i = 0, vn = first_el(node, "value"); vn != NULL; vn = next_el(vn), i++) {
		value = get_ctext(vn);
		row_idx = (layout->intervals[i].start_valid_time - dwml->base_time) / 3600;
		if (row_idx < 0 || row_idx >= MAX_HOURS)
			continue;

		switch (etemp) {
			case TEMPERATURE_MAXIMUM:
				dwml->table[row_idx].temp_max.celcius = atoi(value);
				dwml->table[row_idx].temp_max.has_value = true;
				break;

			case TEMPERATURE_MINIMUM:
				dwml->table[row_idx].temp_min.celcius = atoi(value);
				dwml->table[row_idx].temp_min.has_value = true;
				break;

			case TEMPERATURE_HOURLY:
				dwml->table[row_idx].temp_hourly.celcius = atoi(value);
				dwml->table[row_idx].temp_hourly.has_value = true;
				break;

			case TEMPERATURE_APPARENT:
				dwml->table[row_idx].temp_apparent.celcius = atoi(value);
				dwml->table[row_idx].temp_apparent.has_value = true;
				break;

			default:
				fprintf(stderr, "invalid etype");
				break;
		}
	}
}

static void
parse_weather(struct dwml* dwml, const xmlNodePtr node)
{
	size_t i;
	xmlNodePtr nc, nv;
	struct buf wxbuf;
	int row_idx;
	const struct time_layout *layout = find_layout(dwml, node);

	buf_init(&wxbuf);

	for (i = 0, nc = first_el(node, "weather-conditions"); nc != NULL; nc = next_el(nc), i++) {
		row_idx = (layout->intervals[i].start_valid_time - dwml->base_time) / 3600;

		if (row_idx < 0 || row_idx >= MAX_HOURS)
			continue;

		for (nv = first_el(nc, "value"); nv != NULL; nv = next_el(nv)) {
			const char *coverage = get_attr(nv, "coverage");
			const char *intensity = get_attr(nv, "intensity");
			const char *additive = get_attr(nv, "additive");
			const char *qualifier = get_attr(nv, "qualifier");
			const char *weather_type = get_attr(nv, "weather-type");

			if (strcmp(intensity, "none") == 0)
				intensity = NULL;

			if (strcmp(qualifier, "none") == 0)
				qualifier = NULL;

			if (additive != NULL) {
				if (strcmp(additive, "and") == 0)
					buf_appendf(&wxbuf, ",");
				else
					buf_appendf(&wxbuf, " %s", additive);
			}

			if (intensity != NULL)
				buf_appendf(&wxbuf, " %s", intensity);

			if (strcmp(weather_type, "thunderstorms") == 0)
				weather_type = "TND";
			else if (strcmp(weather_type, "rain showers") == 0)
				weather_type = "SHW";

			if (strcmp(coverage, "slight chance") == 0)
				coverage = "20%";
			else if (strcmp(coverage, "chance") == 0)
				coverage = "40%";
			else if (strcmp(coverage, "likely") == 0)
				coverage = "60%";

			buf_appendf(&wxbuf, " %s %s", weather_type, coverage);

			if (qualifier != NULL)
				buf_appendf(&wxbuf, " (%s)", qualifier);
		}

		if (wxbuf.len > 0) {
			dwml->table[row_idx].weather = strdup(wxbuf.s);
			buf_clean(&wxbuf);
		}
	}
}

static struct row *
get_row(struct dwml* dwml, const struct time_layout *layout, int idx)
{
	int row_idx = (layout->intervals[idx].start_valid_time - dwml->base_time) / 3600;
	if (row_idx < 0 || row_idx >= MAX_HOURS)
		return NULL;

	return &dwml->table[row_idx];
}

static void
parse_parameters(struct dwml* dwml, const xmlNodePtr node)
{
	xmlNodePtr n, vn;
	const struct time_layout *layout;
	size_t i;
	struct row* row;

	for (n = first_el(node, "temperature"); n != NULL; n = next_el(n))
		parse_temperature(dwml, n);

	for (n = first_el(node, "wind-speed"); n != NULL; n = next_el(n)) {
		layout = find_layout(dwml, n);
		for (i = 0, vn = first_el(n, "value"); vn != NULL; vn = next_el(vn), i++) {
			row = get_row(dwml, layout, i);
			if (row == NULL)
				continue;

			row->wind_speed.mps = atoi(get_ctext(vn));
			row->wind_speed.has_value = true;
		}
	}

	for (n = first_el(node, "direction"); n != NULL; n = next_el(n)) {
		layout = find_layout(dwml, n);
		for (i = 0, vn = first_el(n, "value"); vn != NULL; vn = next_el(vn), i++) {
			row = get_row(dwml, layout, i);
			if (row == NULL)
				continue;

			row->wind_dir.degrees = atoi(get_ctext(vn));
			row->wind_dir.has_value = true;
		}
	}

	for (n = first_el(node, "cloud-amount"); n != NULL; n = next_el(n)) {
		layout = find_layout(dwml, n);
		for (i = 0, vn = first_el(n, "value"); vn != NULL; vn = next_el(vn), i++) {
			row = get_row(dwml, layout, i);
			if (row == NULL)
				continue;

			row->cloud_amount.percent = atoi(get_ctext(vn));
			row->cloud_amount.has_value = true;
		}
	}

	for (n = first_el(node, "precipitation"); n != NULL; n = next_el(n)) {
		layout = find_layout(dwml, n);
		for (i = 0, vn = first_el(n, "value"); vn != NULL; vn = next_el(vn), i++) {
			row = get_row(dwml, layout, i);
			if (row == NULL)
				continue;

			row->snow_amount.centimeters = atoi(get_ctext(vn));
			row->snow_amount.has_value = true;
		}
	}

	for (n = first_el(node, "humidity"); n != NULL; n = next_el(n)) {
		layout = find_layout(dwml, n);
		for (i = 0, vn = first_el(n, "value"); vn != NULL; vn = next_el(vn), i++) {
			row = get_row(dwml, layout, i);
			if (row == NULL)
				continue;

			row->humidity.percent = atoi(get_ctext(vn));
			row->humidity.has_value = true;
		}
	}

	for (n = first_el(node, "weather"); n != NULL; n = next_el(n))
		parse_weather(dwml, n);
}

static void
buf_add_temperature(struct buf *buf, const struct temperature *t)
{
	if (t->has_value)
		buf_appendf(buf, "%4d", t->celcius);
	else
		buf_append(buf, "    ", 4);
}

static bool
row_is_empty(const struct row *r)
{
	bool has_value = false;

	has_value |= r->temp_hourly.has_value;
	has_value |= r->temp_apparent.has_value;
	has_value |= r->temp_max.has_value;
	has_value |= r->temp_min.has_value;
	has_value |= r->humidity.has_value;
	has_value |= r->cloud_amount.has_value;
	has_value |= r->wind_speed.has_value;
	has_value |= r->wind_dir.has_value;
	has_value |= r->snow_amount.has_value;
	has_value |= r->weather != NULL;

	return !has_value;
}

static void
format_legend(struct buf *buf, const char *timestr)
{
	struct tm tm;
	time_t now = time(NULL);

	buf_appendf(buf, "\nInfo\n");
	buf_appendf(buf,   "====\n");
	buf_appendf(buf, "UTC:         %s", asctime(gmtime_r(&now, &tm)));
	buf_appendf(buf, "report time: %s\n", timestr);
	buf_appendf(buf, "Legend\n");
	buf_appendf(buf, "======\n");
	buf_appendf(buf, "TMP -- hourly temperature, celsius\n");
	buf_appendf(buf, "APR -- hourly apparent temperature, celsius\n");
	buf_appendf(buf, "MIN -- minimal temperature for a day, celsius\n");
	buf_appendf(buf, "MAX -- maximum temperature for a day, celsius\n");
	buf_appendf(buf, "HUM -- humidity, relative\n");
	buf_appendf(buf, "CLD -- cloud amount, percent\n");
	buf_appendf(buf, "SPD -- wind speed, meters per second\n");
	buf_appendf(buf, "DIR -- wind direction, degrees\n");
	buf_appendf(buf, "SNW -- snow, centimeters\n");
	buf_appendf(buf, "TND -- thunderstorms\n");
	buf_appendf(buf, "SHW -- rain showers\n");
}

static void
format_text_table(struct buf *buf, const struct dwml *dwml, enum legend_position legend_pos)
{
	size_t i, n;
	char timestr[30];
	struct tm tm;
	int prev_day = 0;

	localtime_r(&dwml->base_time, &tm);
	n = strftime(timestr, 30, "%Y-%m-%d %H", &tm);
	
	if (legend_pos == LEGEND_TOP)
		format_legend(buf, timestr);

	buf_appendf(buf, "========== ==  === === === === === === === === === ===================\n");
	buf_appendf(buf, "DATE...... HR  AIR.................... WIND... SNW CONDITIONS.........\n");
	buf_appendf(buf, "               TMP APR MIN MAX HUM CLD SPD DIR    \n");
	buf_appendf(buf, "========== ==  === === === === === === === === === ===================\n");

	for (i = 0; i < MAX_HOURS; i++) {
		const struct row *r = &dwml->table[i];
		if (r->time == 0 || row_is_empty(r))
			continue;

		localtime_r(&r->time, &tm);
		if (prev_day != tm.tm_mday) {
			n = strftime(timestr, 30, "%Y-%m-%d ", &tm);
			buf_append(buf, timestr, n);
			prev_day = tm.tm_mday;
		} else {
			buf_append(buf, "           ", 11);
		}

		buf_appendf(buf, "%02d ", tm.tm_hour);

		buf_add_temperature(buf, &r->temp_hourly);
		buf_add_temperature(buf, &r->temp_apparent);
		buf_add_temperature(buf, &r->temp_min);
		buf_add_temperature(buf, &r->temp_max);

		if (r->humidity.has_value)
			buf_appendf(buf, "%4d", r->humidity.percent);
		else
			buf_append(buf, "    ", 4);

		if (r->cloud_amount.has_value)
			buf_appendf(buf, "%4d", r->cloud_amount.percent);
		else
			buf_append(buf, "    ", 4);

		if (r->wind_speed.has_value)
			buf_appendf(buf, "%4d", r->wind_speed.mps);
		else
			buf_append(buf, "    ", 4);

		if (r->wind_dir.has_value)
			buf_appendf(buf, "%4d", r->wind_dir.degrees);
		else
			buf_append(buf, "    ", 4);

		if (r->snow_amount.has_value)
			buf_appendf(buf, "%4d", r->snow_amount.centimeters);
		else
			buf_append(buf, "    ", 4);

		if (r->weather != NULL)
			buf_appendf(buf, " %s", r->weather);

		buf_append(buf, "\n", 1);
	}

	buf_appendf(buf, "========== ==  === === === === === === === === === ===================\n");

	if (legend_pos == LEGEND_BOTTOM)
		format_legend(buf, timestr);

}

static const char header[] =
	"<tr style=\"background-color: lightsteelblue;\">"
	"<th>HR&nbsp;&nbsp;</th>"
	"<th colspan=\"5\">AIR</th>"
	"<th colspan=\"2\">WIND</th>"
	"<th>SNOW</th>"
	"<th>CONDITIONS</th>"
	"</tr>\n"
	"<tr>"
	"<th></th>"
	"<th>TMP</th>"
	"<th>MIN</th>"
	"<th>MAX</th>"
	"<th>HUM</th>"
	"<th>CLD</th>"
	"<th>SPD</th>"
	"<th>DIR</th>"
	"</tr>\n";

const char *pink_border = " style=\"border: solid 1px pink; border-bottom: none\"";

static void
format_html_table(struct buf *buf, const struct dwml *dwml)
{
	size_t i, n;
	char timestr[30];
	struct tm tm;
	int prev_day = 0;
	const char *style = "";

	localtime_r(&dwml->base_time, &tm);
	n = strftime(timestr, 30, "%Y-%m-%d %H %a", &tm);

	buf_appendf(buf, "<table border=\"0\">\n");
	buf_appendf(buf, header);

	for (i = 0; i < MAX_HOURS; i++) {
		const struct row *r = &dwml->table[i];
		if (r->time == 0 || row_is_empty(r))
			continue;

		localtime_r(&r->time, &tm);
		if (prev_day != tm.tm_mday) {
			buf_appendf(buf, "\n<tr><td colspan=\"12\" style=\"border: lightsteelblue 1px solid;\">");
			n = strftime(timestr, 30, "%Y-%m-%d %a", &tm);
			buf_append(buf, timestr, n);
			buf_appendf(buf, "</td></tr>\n<tr>");
			prev_day = tm.tm_mday;
		} else {
			buf_appendf(buf, "<tr>");
		}

		if (r->temp_max.has_value)
			style = pink_border;

		buf_appendf(buf, "<td>%02d</td>", tm.tm_hour);

//		if (r->temp_hourly.has_value)
//			buf_appendf(buf, "<td>%d</td>", r->temp_hourly.celcius);
//		else
//			buf_appendf(buf, "<td></td>");

		if (r->temp_apparent.has_value)
			buf_appendf(buf, "<td>%d</td>", r->temp_apparent.celcius);
		else
			buf_appendf(buf, "<td></td>");

		if (r->temp_min.has_value)
			buf_appendf(buf, "<td%s>%d</td>", style, r->temp_min.celcius);
		else
			buf_appendf(buf, "<td></td>");

		if (r->temp_max.has_value)
			buf_appendf(buf, "<td%s>%d</td>", style, r->temp_max.celcius);
		else
			buf_appendf(buf, "<td></td>");

		if (r->humidity.has_value)
			buf_appendf(buf, "<td>%d</td>", r->humidity.percent);
		else
			buf_appendf(buf, "<td></td>");

		if (r->cloud_amount.has_value)
			buf_appendf(buf, "<td>%d</td>", r->cloud_amount.percent);
		else
			buf_appendf(buf, "<td></td>");

		if (r->wind_speed.has_value)
			buf_appendf(buf, "<td>%d</td>", r->wind_speed.mps);
		else
			buf_appendf(buf, "<td></td>");

		if (r->wind_dir.has_value)
			buf_appendf(buf, "<td>%d</td>", r->wind_dir.degrees);
		else
			buf_appendf(buf, "<td></td>");

		if (r->snow_amount.has_value && r->snow_amount.centimeters > 0)
			buf_appendf(buf, "<td>%d</td>", r->snow_amount.centimeters);
		else
			buf_appendf(buf, "<td></td>");

		if (r->weather != NULL)
			buf_appendf(buf, "<td>%s</td>", r->weather);
		else
			buf_appendf(buf, "<td></td>");

		buf_appendf(buf, "</tr>\n");
	}

	buf_appendf(buf, "</table>\n");
}

static void
set_rows_time(struct dwml* dwml)
{
	size_t i, j;
	int row_idx;
	struct time_layout *tl;

	for (i = 0; i < dwml->n_layouts; i++) {
		tl = dwml->time_layouts[i];
		for (j = 0; j < tl->count; j++) {
			row_idx = (tl->intervals[j].start_valid_time - dwml->base_time) / 3600;
			if (row_idx < 0 || row_idx >= MAX_HOURS)
				continue;
			dwml->table[row_idx].time = tl->intervals[j].start_valid_time;
		}
	}
}

static void
parse_data(struct dwml* dwml, const xmlNodePtr data_node)
{
	xmlNodePtr n = NULL;
	size_t i = 0;

	dwml->n_layouts = 0;

	for (n = first_el(data_node, "time-layout"); n != NULL; n = next_el(n))
		dwml->n_layouts++;

	dwml->time_layouts = calloc(dwml->n_layouts, sizeof(struct time_layout *));

	for (i = 0, n = first_el(data_node, "time-layout"); n != NULL; n = next_el(n), i++)
		dwml->time_layouts[i] = parse_time_layout(n);

	set_rows_time(dwml);

	for (n = first_el(data_node, "parameters"); n != NULL; n = next_el(n))
		parse_parameters(dwml, n);
}

static struct dwml*
load_dwml(const char *file)
{
	xmlDoc *doc = NULL;
	xmlNodePtr root_element, data;

	doc = xmlReadFile(file, NULL, 0);
	if (doc == NULL)
		err(1, "could not parse file %s", file);

	struct dwml *dwml = calloc(1, sizeof(struct dwml));
//	dwml->base_time = 1440144000;
	dwml->base_time = time(NULL);

	root_element = xmlDocGetRootElement(doc);

	data = first_el(root_element, "data");
	parse_data(dwml, data);

	xmlFreeDoc(doc);

	return dwml;
}

static void
fetch_forecast(const char *fname, const char *url)
{
	if (fetch_url(url, fname) != 0)
		err(1, "cannot fetch forecast for zip %d", zip);
}

static void
version()
{
	printf("weather\n");
	printf("version %s\n", app_version);
	printf("date %s\n", app_date);
	if (strlen(app_diff_stat) > 0) {
		printf("uncommited changes:\n%s\n", app_diff_stat);
		if (debug)
			printf("full diff:\n%s\n", app_diff_full);
	}
}

static void
send_email_to_me(const char *body)
{
	char subject[100];
	sprintf(subject, "wx: weather for zip %05d", zip);
	struct message m = {
		.to = "serge0x76+weather@gmail.com",
		.from = "serge0x76@gmail.com",
		.subject = subject,
		.body = body
	};

	char fname[PATH_MAX];
	snprintf(fname, PATH_MAX, "%s/.config/weather/smtp.txt", getenv("HOME"));
	send_email(&m, fname);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		synopsis();
		return 1;
	}

	int ch;

	while ((ch = getopt_long(argc, argv, "dm:thvz:f:", longopts, NULL)) != -1) {
		switch (ch) {
			case 'd':
				debug = true;
				break;
			case 'z':
				zip = atoi(optarg);
				break;
			case 'm':
				mail_recipients = optarg;
				break;
			case 'f':
				strncpy(fname, optarg, PATH_MAX);
				break;
			case 't':
				html = true;
				break;
			case 'h':
				usage();
				return 1;
			case 'v':
				version();
				return 1;
			default:
				synopsis();
				return 1;
		}
	}

	curl_global_init(CURL_GLOBAL_ALL);

	char timestr[50];
	struct tm tm;
	time_t now = time(NULL);
	char url[1024];
	struct buf out;
	const char *urlfmt =
		"http://graphical.weather.gov/xml/sample_products/browser_interface/ndfdXMLclient.php?"
		"whichClient=NDFDgenMultiZipCode&"
		"zipCodeList=%d&&product=time-series&Unit=m&maxt=maxt&mint=mint&temp=temp&"
		"snow=snow&wspd=wspd&wdir=wdir&sky=sky&wx=wx&rh=rh&appt=appt&wwa=wwa&Submit=Submit";

	localtime_r(&now, &tm);
	buf_init(&out);

	sprintf(url, urlfmt, zip);
	if (debug)
		fprintf(stderr, "zip %d. Fetching %s\n", zip, url);

	if (!debug && *fname == 0) {
		strftime(timestr, 50, "%Y%m%d-%H", &tm);
		sprintf(fname, "%s/.cache/weather/zip-%05d-%s.xml", getenv("HOME"), zip, timestr);
	}

	if (debug)
		fprintf(stderr, "Cached filename: %s\n", fname);

	if (!debug)
		fetch_forecast(fname, url);

	setenv("TZ", "EST5EDT", 1);
	tzset();

	const struct dwml *dwml = load_dwml(fname);

	if (html)
		format_html_table(&out, dwml);
	else
		format_text_table(&out, dwml, LEGEND_TOP);

	if (mail_recipients == NULL)
		puts(out.s);
	else
		send_email_to_me(out.s);

	buf_clean(&out);
	curl_global_cleanup();

	return 0;
}
