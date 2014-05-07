#include "redis.h"
#include "geohash_helper.h"
#include "geojson.h"
#include "geo.h"
#include "zset.h"

/* ====================================================================
 * Bring up / Teardown
 * ==================================================================== */
void *load() { return NULL; }

/* If you reload the module *without* freeing things you allocate in load(),
 * then you *will* introduce memory leaks. */
void cleanup(void *privdata) {}

/* ====================================================================
 * Dynamic Redis API Requirements
 * ==================================================================== */
struct redisModule redisModuleDetail = {
    REDIS_MODULE_COMMAND, /* Tell Dynamic Redis our module provides commands */
    REDIS_VERSION,        /* Provided by redis.h */
    "0.3",                /* Version of this module (only for reporting) */
    "sh.matt.geo",        /* Unique name of this module */
    load,                 /* Load function pointer (optional) */
    cleanup               /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    { "geoadd", geoAddCommand, -5, "s", 0, NULL, 1, 1, 1, 0, 0 },
    { "georadius", geoRadiusCommand, -6, "s", 0, NULL, 1, 1, 1, 0, 0 },
    { "georadiusbymember", geoRadiusByMemberCommand, -5, "s", 0, NULL, 1, 1, 1,
      0, 0 },
    { "geoencode", geoEncodeCommand, -3, "s", 0, NULL, 0, 0, 0, 0, 0 },
    { "geodecode", geoDecodeCommand, -2, "s", 0, NULL, 0, 0, 0, 0, 0 },
    { 0 } /* Always end your command table with {0}
           * If you forget, you will be reminded with a segfault on load. */
};
