#include <stdbool.h>
#include "version.h"
#include "common/struct.h"

struct config {
	const char *dbhost;
	const char *dbname;
	const char *dbuser;
	const char *dbpassword;
	const char *smtp_password_file;
	const char *post_data; /* debug post data */

	char *cache_dir;    /* base dir for next files */
	char *config_fname; /* config file name */

	bool debug;         /* debug output to console */
	bool info;          /* print weather db info */
};

extern struct config cfg;

int init_config(int argc, char **argv);
void dump_config(struct buf *buf);
