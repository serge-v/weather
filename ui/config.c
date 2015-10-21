#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <getopt.h>
#include <err.h>
#include "usage.txt.h"
#include "synopsis.txt.h"

struct config cfg;

static bool
exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

void
dump_config(struct buf *buf)
{
	buf_appendf(buf, "config_file = %s\n", cfg.config_fname);
	buf_appendf(buf, "cache_dir = %s\n", cfg.cache_dir);
	buf_appendf(buf, "dbhost = %s\n", cfg.dbhost);
	buf_appendf(buf, "dbname = %s\n", cfg.dbname);
	buf_appendf(buf, "dbuser = %s\n", cfg.dbuser);
	buf_appendf(buf, "dbpassword = %s\n", cfg.dbpassword);
}

int
init_config(int argc, char **argv)
{
	FILE *f;
	char key[100], value[100];
	int ch, n, line = 0;
	bool show_config = false;

	while ((ch = getopt(argc, argv, "dhvg")) != -1) {
		switch (ch) {
		case 'd':
			cfg.debug = true;
			break;
		case 'g':
			show_config = true;
			break;
		case 'v':
			printf("version: %s\n", app_version);
			printf("date:    %s\n", app_date);
			exit(0);
		case 'h':
			puts(usage_txt);
			return 1;
		default:
			puts(synopsis_txt);
			exit(1);
		}
	}

	const char *home = getenv("HOME");
	if (home == NULL) {
		cfg.cache_dir = strdup("/tmp/weatherui");
		cfg.config_fname = strdup("/etc/weatherui.conf");
	} else {
		asprintf(&cfg.cache_dir, "%s/.cache/weatherui", home);
		asprintf(&cfg.config_fname, "%s/.config/weatherui/weatherui.conf", home);
	}

	f = fopen(cfg.config_fname, "rt");
	if (f == NULL)
		err(1, "cannot open %s", cfg.config_fname);

	while (!feof(f)) {
		line++;
		n = fscanf(f, "%s = %s\n", key, value);
		if (n == -1)
			continue;

		if (n != 2)
			err(1, "%s:%d: invalid config line.", cfg.config_fname, line);

		if (key[0] == '#') {
			continue;
		} else if (strcmp("dbhost", key) == 0) {
			cfg.dbhost = strdup(value);
		} else if (strcmp("dbname", key) == 0) {
			cfg.dbname = strdup(value);
		} else if (strcmp("dbuser", key) == 0) {
			cfg.dbuser = strdup(value);
		} else if (strcmp("dbpassword", key) == 0) {
			cfg.dbpassword = strdup(value);
		} else if (strcmp("cache_dir", key) == 0) {
			cfg.cache_dir = strdup(value);
		}
	}
	
	fclose(f);

	if (!exists(cfg.cache_dir)) {
		if (mkdir(cfg.cache_dir, 0777) != 0)
			err(1, "Cannot create dir %s", cfg.cache_dir);
	}

	if (show_config) {
		struct buf buf;
		buf_init(&buf);
		dump_config(&buf);
		puts(buf.s);
		buf_clean(&buf);
		exit(1);
	}

	return 0;
}
