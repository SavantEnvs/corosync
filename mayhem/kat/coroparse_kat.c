/* Known-answer probe for corosync's configuration parser: parse a file, dump the resulting icmap. */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <corosync/corotypes.h>
#include <corosync/icmap.h>
#include "../../exec/main.h"

static const char *conf_path;

const char *corosync_get_config_file(void)
{
	return conf_path;
}

static void print_value(icmap_map_t map, const char *key, icmap_value_types_t type, size_t len)
{
	union {
		int8_t i8; uint8_t u8; int16_t i16; uint16_t u16; int32_t i32; uint32_t u32;
		int64_t i64; uint64_t u64; float f; double d; char *s;
	} v;
	char *str;
	size_t l = sizeof(v);
	icmap_value_types_t t;

	l = sizeof(v);
	switch (type) {
	case ICMAP_VALUETYPE_INT8: icmap_get_r(map, key, &v.i8, &l, &t); printf("%s (i8) = %d\n", key, v.i8); break;
	case ICMAP_VALUETYPE_UINT8: icmap_get_r(map, key, &v.u8, &l, &t); printf("%s (u8) = %u\n", key, v.u8); break;
	case ICMAP_VALUETYPE_INT16: icmap_get_r(map, key, &v.i16, &l, &t); printf("%s (i16) = %d\n", key, v.i16); break;
	case ICMAP_VALUETYPE_UINT16: icmap_get_r(map, key, &v.u16, &l, &t); printf("%s (u16) = %u\n", key, v.u16); break;
	case ICMAP_VALUETYPE_INT32: icmap_get_r(map, key, &v.i32, &l, &t); printf("%s (i32) = %d\n", key, v.i32); break;
	case ICMAP_VALUETYPE_UINT32: icmap_get_r(map, key, &v.u32, &l, &t); printf("%s (u32) = %u\n", key, v.u32); break;
	case ICMAP_VALUETYPE_INT64: icmap_get_r(map, key, &v.i64, &l, &t); printf("%s (i64) = %" PRId64 "\n", key, v.i64); break;
	case ICMAP_VALUETYPE_UINT64: icmap_get_r(map, key, &v.u64, &l, &t); printf("%s (u64) = %" PRIu64 "\n", key, v.u64); break;
	case ICMAP_VALUETYPE_FLOAT: icmap_get_r(map, key, &v.f, &l, &t); printf("%s (float) = %.3f\n", key, v.f); break;
	case ICMAP_VALUETYPE_DOUBLE: icmap_get_r(map, key, &v.d, &l, &t); printf("%s (double) = %.3f\n", key, v.d); break;
	case ICMAP_VALUETYPE_STRING:
		if (icmap_get_string_r(map, key, &str) == CS_OK) {
			printf("%s (str) = %s\n", key, str);
			free(str);
		}
		break;
	default:
		printf("%s (binary) len=%zu\n", key, len);
		break;
	}
}

int main(int argc, char **argv)
{
	icmap_map_t map;
	icmap_iter_t iter;
	const char *error_string = NULL;
	const char *key;
	size_t len;
	icmap_value_types_t type;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <corosync.conf>\n", argv[0]);
		return 2;
	}
	conf_path = argv[1];
	if (icmap_init_r(&map) != CS_OK) {
		return 2;
	}
	if (coroparse_configparse(map, &error_string) != 0) {
		printf("parse error: %s\n", error_string ? error_string : "(none)");
		return 1;
	}
	iter = icmap_iter_init_r(map, NULL);
	while ((key = icmap_iter_next(iter, &len, &type)) != NULL) {
		print_value(map, key, type, len);
	}
	icmap_iter_finalize(iter);
	icmap_fini_r(map);
	return 0;
}
