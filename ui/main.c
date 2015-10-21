#include "config.h"
#include "common/mysql.h"
#include "common/crypt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <inttypes.h>

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
	my_bool error[3];

	unsigned long long user_id = 0;
	char buf_zip[50];
	char confirm_code[50];
	MYSQL_TIME created;

	snprintf(confirm_code, 50, "%" PRIx64, get_random());

	const char *qselect = "select user_id, zip, created from USER where email = ?;";
	const char *qinsert = "insert into USER(email, zip, confirm_code) values(?, ?, ?);";
	const char *qupdate = "update USER set zip = ?, confirm_code = ? where email = ?;";

	memset(param, 0, sizeof(param));

	par_length[0] = strlen(email);
	param[0].buffer_type = MYSQL_TYPE_STRING;
	param[0].buffer = (void *)email;
	param[0].length = &par_length[0];

	par_length[1] = strlen(zip);
	param[1].buffer_type = MYSQL_TYPE_STRING;
	param[1].buffer = (char *)zip;
	param[1].length = &par_length[1];

	par_length[2] = strlen(confirm_code);
	param[2].buffer_type = MYSQL_TYPE_STRING;
	param[2].buffer = &confirm_code;
	param[2].length = &par_length[2];

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

		MYSQL_BIND uparam[3];

		uparam[0] = param[1]; // zip
		uparam[1] = param[2]; // confirm
		uparam[2] = param[0]; // email

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
		    "If you didn't subscribe for weather report just ignore this email.\n\n"
		    "To confirm and set options go to http://voilokov.com?code=%s\n\n"
		    "Regards,\nWetreps\n(Weather Reports by Email).\n",
		    confirm_code);
}


int main(int argc, char **argv)
{
	if (init_config(argc, argv) != 0)
		return 1;

	struct buf confirm_email;
	struct params p;
	buf_init(&ebuf);
	buf_init(&obuf);
	parse_request(&p);

	if (ebuf.len > 0) {
		printf("ERROR: %s\n", ebuf.s);
		exit(1);
	}

	buf_init(&confirm_email);
	mysql = db_open(cfg.dbhost, cfg.dbname, cfg.dbuser, cfg.dbpassword);
	create_user("serge0x76.com", "10010", &confirm_email);

	puts(confirm_email.s);
}
