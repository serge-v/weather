#include <stdbool.h>
#include "version.h"
#include "common/struct.h"

struct config {
	const char *dbhost;
	const char *dbname;
	const char *dbuser;
	const char *dbpassword;

	char *cache_dir;    /* base dir for next files */
	char *config_fname; /* config file name */

	bool debug;         /* debug output to console */
};

extern struct config cfg;

int init_config(int argc, char **argv);
void dump_config(struct buf *buf);
