#include "config.h"
#include "common/mysql.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

struct params {
	const char *email;
	const char *zip;
	const char *time;
};

static MYSQL *mysql;
static struct buf ebuf;
static struct buf obuf;

static void
parse_request(struct params *p)
{
	char *var = getenv("QUERY_STRING");
	if (var == NULL)
		return;

	char *query = strdup(var);

	char *ptr = strstr(query, "email=");
	p->email = ptr + 6;
	ptr = strstr(query, "zip=");
	p->zip = ptr + 4;
	ptr = strstr(query, "time=");
	p->time = ptr + 5;

	ptr = query;
	while ((ptr = strchr(ptr, '&')) != NULL) {
		*ptr = 0;
		ptr++;
	}

	if (*p->email == 0)
		buf_appendf(&ebuf, "email is empty.");

	if (*p->zip == 0)
		buf_appendf(&ebuf, "zip is empty.");

	if (*p->time == 0)
		buf_appendf(&ebuf, "time is empty.");
}

static void
create_user(const char *email, const char *zip)
{
	int rc;
	MYSQL_STMT *stmt, *stmt1;
	MYSQL_BIND param[2];
	MYSQL_BIND column[3];

	unsigned long par_length[2];
	unsigned long col_length[3];
	my_bool error[3];

	unsigned long long user_id = 0;
	char buf_zip[50];
	MYSQL_TIME created;

	const char *qselect = "select user_id, zip, created from USER where email = ?;";
	const char *qinsert = "insert into USER(email, zip, created) values(?, ?, curtime());";

	memset(param, 0, sizeof(param));

	par_length[0] = strlen(email);
	param[0].buffer_type = MYSQL_TYPE_STRING;
	param[0].buffer = (void *)email;
	param[0].length = &par_length[0];

	par_length[1] = strlen(zip);
	param[1].buffer_type = MYSQL_TYPE_STRING;
	param[1].buffer = (char *)zip;
	param[1].length = &par_length[1];

	memset(column, 0, sizeof(column));

	column[0].buffer_type = MYSQL_TYPE_LONG;
	column[0].buffer_length = sizeof(user_id);
	column[0].buffer = &user_id;
	column[0].length = &col_length[0];
	column[0].error = &error[0];

	column[1].buffer_type = MYSQL_TYPE_STRING;
	column[1].buffer = (char *)buf_zip;
	column[1].buffer_length = sizeof(buf_zip);
	column[1].length = &col_length[1];
	column[1].error = &error[1];

	column[2].buffer_type = MYSQL_TYPE_TIMESTAMP;
	column[2].buffer_length = sizeof(created);
	column[2].buffer = &created;
	column[2].length = &col_length[2];
	column[2].error = &error[2];

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
	}


	if (rc != 0)
		errx(1, "cannot fetch stmt. %s", mysql_error(mysql));

	mysql_stmt_close(stmt);

	if (cfg.debug)
		fprintf(stderr, "user: %llu, %s\n", user_id, zip);
}


int main(int argc, char **argv)
{
	if (init_config(argc, argv) != 0)
		return 1;

	struct params p;
	buf_init(&ebuf);
	buf_init(&obuf);
	parse_request(&p);

	if (ebuf.len > 0) {
		printf("ERROR: %s\n", ebuf.s);
		exit(1);
	}

	mysql = db_open(cfg.dbhost, cfg.dbname, cfg.dbuser, cfg.dbpassword);
	create_user("aa@bbb.com", "10010");
}
