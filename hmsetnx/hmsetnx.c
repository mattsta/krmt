#include "redis.h"

/* ====================================================================
 * Redis Add-on Module: hmsetnx
 * Provides commands: hmsetnx
 * Behaviors:
 *   - hmsetnx - hash multi-set only if fields don't exist
 * ==================================================================== */

/* ====================================================================
 * Command Implementations
 * ==================================================================== */
void hmsetnxCommand(redisClient *c) {
    /* args 0-N: ["HMSETNX", key, field1, val1, field2, val2, ...] */
    /* Returns 0 if no keys were set or OK if keys were set. */
    int i, busykeys = 0;
    robj *o;

    if ((c->argc % 2) == 1) {
        addReplyError(c, "wrong number of arguments for HMSETNX");
        return;
    }

    if ((o = hashTypeLookupWriteOrCreate(c, c->argv[1])) == NULL)
        return;
    hashTypeTryConversion(o, c->argv, 2, c->argc - 1);

    /* Handle the NX flag. The HMSETNX semantic is to return zero and don't
     * set nothing at all if at least one already key exists. */
    for (i = 2; i < c->argc; i += 2) {
        if (hashTypeExists(o, c->argv[i])) {
            busykeys++;
        }
        if (busykeys) {
            addReply(c, shared.czero);
            return;
        }
    }

    hmsetCommand(c);
}

/* ====================================================================
 * Bring up / Teardown
 * ==================================================================== */
void *load() {
    return NULL;
}

/* If you reload the module *without* freeing things you allocate in load(),
 * then you *will* introduce memory leaks. */
void cleanup(void *privdata) {
}

/* ====================================================================
 * Dynamic Redis API Requirements
 * ==================================================================== */
struct redisModule redisModuleDetail = {
    REDIS_MODULE_COMMAND, /* Tell Dynamic Redis our module provides commands */
    REDIS_VERSION,        /* Provided by redis.h */
    "0.4",                /* Version of this module (only for reporting) */
    "sh.matt.hmsetnx",    /* Unique name of this module */
    load,                 /* Load function pointer (optional) */
    cleanup               /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    {"hmsetnx", hmsetnxCommand, -4, "wm", 0, NULL, 1, 1, 1, 0, 0},
    {0} /* Always end your command table with {0}
         * If you forget, you will be reminded with a segfault on load. */
};
