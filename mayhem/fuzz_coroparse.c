/*
 * Fuzz corosync's corosync.conf parser (exec/coroparse.c): the bytes become the main configuration
 * file, parsed into a fresh icmap exactly as `corosync` does at start-up.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <corosync/corotypes.h>
#include <corosync/icmap.h>

#include "../exec/main.h"

static char conf_path[512];

const char *corosync_get_config_file(void)
{
	return conf_path;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	const char *tmpdir = getenv("TMPDIR");

	if (tmpdir == NULL || tmpdir[0] == '\0') {
		tmpdir = "/tmp";
	}
	snprintf(conf_path, sizeof(conf_path), "%s/corosync-fuzz-%d.conf", tmpdir, (int)getpid());
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	FILE *fp;
	icmap_map_t map;
	const char *error_string = NULL;

	fp = fopen(conf_path, "w");
	if (fp == NULL) {
		abort();
	}
	if (size > 0 && fwrite(data, 1, size, fp) != size) {
		abort();
	}
	fclose(fp);

	if (icmap_init_r(&map) != CS_OK) {
		abort();
	}
	(void)coroparse_configparse(map, &error_string);
	icmap_fini_r(map);

	return 0;
}
