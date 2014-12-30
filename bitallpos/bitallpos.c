#include "redis.h"

/* ====================================================================
 * Redis Add-on Module: bitallpos
 * Provides commands: bitallpos
 * Behaviors:
 *   - bitallpos - Return positions of all set bits
 * Origin: https://github.com/antirez/redis/pull/1295
 * https://github.com/jonahharris/redis/commit/fb813b3196e399ac49e1720c824d6cf37fa6af8f
 * ==================================================================== */

/* ====================================================================
 * Command Implementations
 * ==================================================================== */
/* BITPOS key [start end limit] */
static void bitallposCommand(redisClient *c) {
    robj *o;
    long start, end, limit = -1, strlen;
    void *replylen = NULL;
    unsigned char *p, byte;
    char llbuf[32];
    unsigned long bytecount = 0;
    unsigned long bitcount = 0;

    /* Lookup, check for type, and return 0 for non existing keys. */
    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.emptymultibulk)) ==
            NULL ||
        checkType(c, o, REDIS_STRING))
        return;

    /* Set the 'p' pointer to the string, that can be just a stack allocated
     * array if our string was integer encoded. */
    if (o->encoding == REDIS_ENCODING_INT) {
        p = (unsigned char *)llbuf;
        strlen = ll2string(llbuf, sizeof(llbuf), (long)o->ptr);
    } else {
        p = (unsigned char *)o->ptr;
        strlen = sdslen(o->ptr);
    }

    /* Parse start/end range if any. */
    if (c->argc == 5) {
        if (getLongFromObjectOrReply(c, c->argv[4], &limit, NULL) != REDIS_OK)
            return;
    }
    if (c->argc >= 4) {
        if (getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != REDIS_OK)
            return;
        if (getLongFromObjectOrReply(c, c->argv[3], &end, NULL) != REDIS_OK)
            return;
    } else if (c->argc == 3) {
        if (getLongFromObjectOrReply(c, c->argv[2], &start, NULL) != REDIS_OK)
            return;
        end = strlen - 1;
    } else if (c->argc == 2) {
        /* The whole string. */
        start = 0;
        end = strlen - 1;
    } else {
        /* Syntax error. */
        addReply(c, shared.syntaxerr);
        return;
    }

    /* Convert negative indexes */
    if (start < 0)
        start = strlen + start;
    if (end < 0)
        end = strlen + end;
    if (start < 0)
        start = 0;
    if (end < 0)
        end = 0;
    if (end >= strlen)
        end = strlen - 1;

    /* Precondition: end >= 0 && end < strlen, so the only condition where
     * zero can be returned is: start > end. */
    if (start > end) {
        addReply(c, shared.emptymultibulk);
        return;
    }

    p = (p + start);
    bytecount = (end - start + 1);
    replylen = addDeferredMultiBulkLength(c);

    /* iterate over bytes */
    while (bytecount--) {
        unsigned int i = 128, pos = 0;
        byte = *p++;
        while (byte && limit) {
            if (byte & i) {
                addReplyBulkLongLong(c, (start * 8 + pos));
                byte &= ~(1 << (7 - pos));
                ++bitcount;
                --limit;
                limit = (((-1) > (limit)) ? (-1) : (limit));
            }
            i >>= 1;
            pos++;
        }
        start++;
    }

    setDeferredMultiBulkLength(c, replylen, bitcount);
}

/* ====================================================================
 * Bring up / Teardown
 * ==================================================================== */
void *load() { return NULL; }

/* If you reload the module *without* freeing things you allocate in load(),
 * then you *will* introduce memory leaks. */
void cleanup(void *privdata) { return; }

/* ====================================================================
 * Dynamic Redis API Requirements
 * ==================================================================== */
struct redisModule redisModuleDetail = {
    REDIS_MODULE_COMMAND, /* Tell Dynamic Redis our module provides commands */
    REDIS_VERSION,        /* Provided by redis.h */
    "0.3",                /* Version of this module (only for reporting) */
    "com.github.antirez.redis.pullrequest.1295", /* Unique name of this module
                                                    */
    load,   /* Load function pointer (optional) */
    cleanup /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    {"bitallpos", bitallposCommand, -2, "r", 0, NULL, 1, 1, 1, 0, 0},
    {0} /* Always end your command table with {0}
         * If you forget, you will be reminded with a segfault on load. */
};
