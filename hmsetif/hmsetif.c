#include "redis.h"

/* ====================================================================
 * Redis Add-on Module: hmsetif
 * Provides commands: hmsetif
 * Behaviors:
 *   - hmsetif - only set multiple values if the first condition matches
 * ==================================================================== */

/* ====================================================================
 * Command Implementations
 * ==================================================================== */
/* HSETCK myhash checkkey checkval setkey setval */
void hmsetifCommand(redisClient *c) {
    if ((c->argc % 2) == 1) {
        addReplyError(c, "wrong number of arguments for HMSETIF");
        return;
    }

    robj *o;
    if ((o = hashTypeLookupWriteOrCreate(c, c->argv[1])) == NULL)
        return;

    if (!hashTypeExists(o, c->argv[2])) {
        addReply(c, shared.czero);
        return;
    }

    robj *testobj = hashTypeGetObject(o, c->argv[2]);
    if (!equalStringObjects(testobj, c->argv[3])) {
        addReply(c, shared.czero);
        decrRefCount(testobj);
        return;
    }
    decrRefCount(testobj);

    /* Rearrange command vector for direct hmset */
    /* We need to: remove the checkkey and checkval.  Instead
     * of rewriting the entire command vector, we
     * can accomplish it by: free checkkey/checkval then
     * move the last setkey/setval into the position of checkkey/checkval. */
    decrRefCount(c->argv[2]);
    decrRefCount(c->argv[3]);
    c->argv[2] = c->argv[c->argc - 2];
    c->argv[3] = c->argv[c->argc - 1];
    c->argc -= 2;
    hmsetCommand(c);
}

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
    "sh.matt.hmsetif",    /* Unique name of this module */
    load,                 /* Load function pointer (optional) */
    cleanup               /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    {"hmsetif", hmsetifCommand, -6, "wm", 0, NULL, 1, 1, 1, 0, 0},
    {0} /* Always end your command table with {0}
         * If you forget, you will be reminded with a segfault on load. */
};
