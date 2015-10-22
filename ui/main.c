#include "config.h"
#include "common/mysql.h"
#include "common/crypt.h"
#include "common/net.h"
#include "common/regexp.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <err.h>
#include <inttypes.h>
#include <curl/curl.h>
#include "main.html.h"
#include "confirm.html.h"

struct params {
	const char *email;
	const char *zip;
	const char *time;
	const char *code;
	const char *confirm_code;
	bool email_sent;
};

struct user
{
	char *email;
	char *zip;
	char *schedule;
};

static MYSQL *mysql;
static struct buf ebuf;
static struct buf obuf;
static char post_data[1024];
static char base_url[1024];

static void
init_base_url()
{
	snprintf(base_url, 1024, "%s%s", getenv("HTTP_HOST"), getenv("SCRIPT_NAME"));
}

static void
parse_post_data(struct params *p)
{
	if (cfg.post_data != NULL) {
		strcpy(post_data, cfg.post_data);
	} else {
		char *str = getenv("CONTENT_LENGTH");
		size_t len = atoi(str);
		len = fread(post_data, 1, len, stdin);
		CURL *curl = curl_easy_init();
		char *output = curl_easy_unescape(curl, post_data, len, NULL);
		strcpy(post_data, output);
		curl_free(output);
	}

	char *query = post_data;
	char *ptr = strstr(query, "email=");

	if (ptr != NULL)
		p->email = ptr + 6;

	ptr = strstr(query, "zip=");
	if (ptr != NULL)
		p->zip = ptr + 4;

	ptr = strstr(query, "time=");
	if (ptr != NULL)
		p->time = ptr + 5;

	ptr = strstr(query, "confirm=");
	if (ptr != NULL)
		p->confirm_code = ptr + 8;

	ptr = query;
	while ((ptr = strchr(ptr, '&')) != NULL) {
		*ptr = 0;
		ptr++;
	}
}

static void
parse_request(struct params *p)
{
	post_data[0] = 0;

	char *method = getenv("REQUEST_METHOD");
	if (method != NULL && strcmp(method, "POST") == 0) {
		parse_post_data(p);
	}

	char *var = getenv("QUERY_STRING");
	if (var == NULL || *var == 0)
		return;

	char *query = strdup(var);

	if (strncmp(query, "code=", 5) == 0)
		p->code = query + 5;
	else if (strcmp(query, "emailsent=1") == 0)
		p->email_sent = true;
}

static uint64_t
get_random()
{
	uint64_t num = 0;
	size_t was_read;

	FILE *f = fopen("/dev/urandom", "r");
	if (f == NULL)
		err(1, "cannot open urandom");
	was_read = fread(&num, sizeof(uint64_t), 1, f);
	if (was_read != 1)
		errx(1, "cannot read from urandom");

	return num;
}

static void
create_user(const char *email, const char *zip, struct buf *confirm_email)
{
	int rc;
	MYSQL_STMT *stmt, *stmt1;
	MYSQL_BIND param[3];
	MYSQL_BIND column[3];

	unsigned long par_length[3];
	unsigned long col_length[3];

	unsigned long long user_id = 0;
	char buf_zip[50];
	char confirm_code[50];
	MYSQL_TIME created;

	snprintf(confirm_code, 50, "%" PRIx64, get_random());
//	snprintf(confirm_code, 50, "%" PRIx64, 0x123LLU);

	const char *qselect = "select user_id, zip, created from USER where email = ?;";
	const char *qinsert = "insert into USER(email, zip, confirm_code) values(?, ?, ?);";
	const char *qupdate = "update USER set confirm_code = ? where email = ?;";

	memset(param, 0, sizeof(param));

	par_length[0] = strlen(email);
	param[0].buffer_type = MYSQL_TYPE_STRING;
	param[0].buffer = (void *)email;
	param[0].length = &par_length[0];

	par_length[1] = strlen(zip);
	param[1].buffer_type = MYSQL_TYPE_STRING;
	param[1].buffer = (void *)zip;
	param[1].length = &par_length[1];

	par_length[2] = strlen(confirm_code);
	param[2].buffer_type = MYSQL_TYPE_STRING;
	param[2].buffer = (void *)confirm_code;
	param[2].length = &par_length[2];

	memset(column, 0, sizeof(column));

	column[0].buffer_type = MYSQL_TYPE_LONG;
	column[0].buffer_length = sizeof(user_id);
	column[0].buffer = &user_id;
	column[0].length = &col_length[0];

	column[1].buffer_type = MYSQL_TYPE_STRING;
	column[1].buffer = (void *)buf_zip;
	column[1].buffer_length = sizeof(buf_zip);
	column[1].length = &col_length[1];

	column[2].buffer_type = MYSQL_TYPE_TIMESTAMP;
	column[2].buffer_length = sizeof(created);
	column[2].buffer = &created;
	column[2].length = &col_length[2];

	stmt = mysql_stmt_init(mysql);
	rc = mysql_stmt_prepare(stmt, qselect, strlen(qselect));
	if (rc != 0)
		errx(1, "cannot prepare select query. %s", mysql_error(mysql));

	rc = mysql_stmt_bind_param(stmt, param);
	if (rc != 0)
		errx(1, "cannot bind params. %s", mysql_error(mysql));

	rc = mysql_stmt_bind_result(stmt, column);
	if (rc != 0)
		errx(1, "cannot bind params. %s", mysql_error(mysql));

	rc = mysql_stmt_execute(stmt);
	if (rc != 0)
		errx(1, "cannot execute stmt. %s", mysql_error(mysql));

	rc = mysql_stmt_fetch(stmt);

	if (rc == MYSQL_NO_DATA) {

		/* insert user */

		stmt1 = mysql_stmt_init(mysql);

		rc = mysql_stmt_prepare(stmt1, qinsert, strlen(qinsert));
		if (rc != 0)
			errx(1, "cannot prepare insert query. %s", mysql_error(mysql));

		rc = mysql_stmt_bind_param(stmt1, param);
		if (rc != 0)
			errx(1, "cannot bind params. %s", mysql_error(mysql));

		rc = mysql_stmt_execute(stmt1);
		if (rc != 0)
			errx(1, "cannot execute stmt. %s", mysql_error(mysql));

		mysql_stmt_close(stmt1);

		/* select again */

		rc = mysql_stmt_execute(stmt);
		if (rc != 0)
			errx(1, "cannot execute stmt. %s", mysql_error(mysql));

		rc = mysql_stmt_fetch(stmt);
		if (rc != 0)
			errx(1, "cannot fetch stmt. %s", mysql_error(mysql));

		if (cfg.debug)
			fprintf(stderr, "user created: %llu, %s, %s\n", user_id, zip, confirm_code);

		mysql_stmt_close(stmt);
	} else {
		/* update user */

		mysql_stmt_close(stmt);

		stmt1 = mysql_stmt_init(mysql);

		rc = mysql_stmt_prepare(stmt1, qupdate, strlen(qupdate));
		if (rc != 0)
			errx(1, "cannot prepare update query. %s", mysql_error(mysql));

		MYSQL_BIND uparam[2];

		uparam[0] = param[2]; // confirm
		uparam[1] = param[0]; // email

		rc = mysql_stmt_bind_param(stmt1, uparam);
		if (rc != 0)
			errx(1, "cannot bind update params. %s", mysql_error(mysql));

		rc = mysql_stmt_execute(stmt1);
		if (rc != 0)
			errx(1, "cannot execute stmt. %s", mysql_error(mysql));

		mysql_stmt_close(stmt1);

		if (cfg.debug)
			fprintf(stderr, "user updated: %llu, %s, %s\n", user_id, zip, confirm_code);
	}

	buf_appendf(confirm_email,
		    "<pre>\n"
		    "If you didn't subscribe for weather report from Wetreps just ignore this email.\n\n"
		    "To confirm subscription and set options go to link:\n"
		    "<a href=\"http://localhost:8000/weatherui?code=%s\">http://localhost:8000/weatherui?code=%s</a>\n\n"
		    "Regards,\nWetreps\n(Weather Report Robots).\n"
		    "</pre>\n",
		    confirm_code, confirm_code);
}

static bool
get_user(const char *code, struct user *user)
{
	int rc;
	MYSQL_STMT *stmt;
	MYSQL_BIND param[1];
	MYSQL_BIND column[3];

	unsigned long par_length[1];
	unsigned long col_length[3];

	char buf_email[250];
	char buf_zip[50];
	char buf_schedule[250];

	const char *qselect = "select email, zip, schedule from USER where confirm_code = ?;";

	memset(param, 0, sizeof(param));

	par_length[0] = strlen(code);
	param[0].buffer_type = MYSQL_TYPE_STRING;
	param[0].buffer = (void *)code;
	param[0].length = &par_length[0];

	memset(column, 0, sizeof(column));

	column[0].buffer_type = MYSQL_TYPE_STRING;
	column[0].buffer_length = sizeof(buf_email);
	column[0].buffer = (void *)buf_email;
	column[0].length = &col_length[0];

	column[1].buffer_type = MYSQL_TYPE_STRING;
	column[1].buffer = (void *)buf_zip;
	column[1].buffer_length = sizeof(buf_zip);
	column[1].length = &col_length[1];

	column[2].buffer_type = MYSQL_TYPE_STRING;
	column[2].buffer_length = sizeof(buf_schedule);
	column[2].buffer = (void *)buf_schedule;
	column[2].length = &col_length[2];

	stmt = mysql_stmt_init(mysql);
	rc = mysql_stmt_prepare(stmt, qselect, strlen(qselect));
	if (rc != 0)
		errx(1, "cannot prepare select query. %s", mysql_error(mysql));

	rc = mysql_stmt_bind_param(stmt, param);
	if (rc != 0)
		errx(1, "cannot bind params. %s", mysql_error(mysql));

	rc = mysql_stmt_bind_result(stmt, column);
	if (rc != 0)
		errx(1, "cannot bind columns. %s", mysql_error(mysql));

	rc = mysql_stmt_execute(stmt);
	if (rc != 0)
		errx(1, "cannot execute stmt. %s", mysql_error(mysql));

	rc = mysql_stmt_fetch(stmt);

	if (rc == MYSQL_NO_DATA)
		return false;

	if (rc != 0)
		errx(1, "cannot execute stmt. %s", mysql_error(mysql));

	if (cfg.debug)
		fprintf(stderr, "user get: %s, %s, %s\n", buf_email, buf_zip, buf_schedule);
		
	user->email = strdup(buf_email);
	user->zip = strdup(buf_zip);
	user->schedule = strdup(buf_schedule);

	mysql_stmt_close(stmt);

	return true;
}

static void
confirm_user(const char *code, const char *zip, const char *schedule)
{
	int rc;
	MYSQL_STMT *stmt;
	MYSQL_BIND param[3];
	unsigned long par_length[3];

	const char *qupdate = "update USER set zip = ?, schedule = ?, confirm_code = NULL where confirm_code = ?;";

	memset(param, 0, sizeof(param));

	par_length[0] = strlen(zip);
	param[0].buffer_type = MYSQL_TYPE_STRING;
	param[0].buffer = (void *)zip;
	param[0].length = &par_length[0];

	par_length[1] = strlen(schedule);
	param[1].buffer_type = MYSQL_TYPE_STRING;
	param[1].buffer = (void *)schedule;
	param[1].length = &par_length[1];

	par_length[2] = strlen(code);
	param[2].buffer_type = MYSQL_TYPE_STRING;
	param[2].buffer = (void *)code;
	param[2].length = &par_length[2];

	stmt = mysql_stmt_init(mysql);
	rc = mysql_stmt_prepare(stmt, qupdate, strlen(qupdate));
	if (rc != 0)
		errx(1, "cannot prepare update query. %s", mysql_error(mysql));

	rc = mysql_stmt_bind_param(stmt, param);
	if (rc != 0)
		errx(1, "cannot bind params. %s", mysql_error(mysql));

	rc = mysql_stmt_execute(stmt);
	if (rc != 0)
		errx(1, "cannot execute stmt. %s", mysql_error(mysql));

	my_ulonglong rows = mysql_stmt_affected_rows(stmt);
	mysql_stmt_close(stmt);
}

static void
send_wetreps_email(const char *to, const char *body)
{
	struct message m = {
		.to = to,
		.from = "serge0x76+wetreps@gmail.com",
		.subject = "wetreps",
		.body = body
	};

	send_email(&m, cfg.smtp_password_file);
}

int main(int argc, char **argv, char **envp)
{
	if (init_config(argc, argv) != 0)
		return 1;

	struct buf confirm_email, page;
	struct params p;
	memset(&p, 0, sizeof(struct params));

	buf_init(&page);
	buf_init(&ebuf);
	buf_init(&obuf);
	init_base_url();
	parse_request(&p);

	if (ebuf.len > 0) {
		printf("ERROR: %s\n", ebuf.s);
		exit(1);
	}

	if (p.confirm_code != NULL) {
		struct user user;
		mysql = db_open(cfg.dbhost, cfg.dbname, cfg.dbuser, cfg.dbpassword);
		if (!get_user(p.confirm_code, &user)) {
			buf_appendf(&page, "Invalid confirmation code.");
			goto flush;
		}
		confirm_user(p.confirm_code, p.zip, p.time);
		buf_appendf(&page, "<pre>Subscription confirmed.</pre>");
		goto flush;
	}

	if (p.code != NULL) {
		struct user user;
		mysql = db_open(cfg.dbhost, cfg.dbname, cfg.dbuser, cfg.dbpassword);
		if (!get_user(p.code, &user)) {
			buf_appendf(&page, "Invalid confirmation code.");
			goto flush;
		}

		buf_append(&page, confirm_html, confirm_html_size);
		buf_replace(&page, "\\{email\\}", user.email);
		buf_replace(&page, "\\{zip\\}", user.zip);
		buf_replace(&page, "\\{schedule\\}", user.schedule);
		buf_replace(&page, "\\{code\\}", p.code);
		goto flush;
	}

	if (p.email_sent) {
		buf_appendf(&page,
			"<pre>"
			"Confirmation email sent to you.\n"
			"Please open email with subject 'wetreps' in your mailbox and \n"
			"go to enclosed link to confirm delivery options.\n"
			"</pre>");
		goto flush;
	}
	
	if (p.email != NULL) {
		buf_init(&confirm_email);
		mysql = db_open(cfg.dbhost, cfg.dbname, cfg.dbuser, cfg.dbpassword);
		create_user(p.email, "10001", &confirm_email);
		send_wetreps_email(p.email, confirm_email.s);
		printf("Status: 302 Moved\r\n");
		printf("Location: ?emailsent=1\r\n\r\n");
		return 0;
	}

	buf_append(&page, main_html, main_html_size);

flush:
/*	buf_appendf(&page, "<pre>\n");
	buf_appendf(&page, "post_data: %s\n", post_data);
	for (char **env = envp; *env != 0; env++) {
		buf_appendf(&page, "%s\n", *env);
	}
	buf_appendf(&page, "</pre>\n");
*/
	printf("Content-type: text/html\r\n");
	printf("Content-length: %zu\r\n\r\n", page.len);
	puts(page.s);
}
