#include "redis.h"

void pingCommandLocal(redisClient *c) {
    sds pooong = sdsnew("+POOOOOOOOOONG\r\n");
    /* Redis uses refcounted objects.  You must decrement the refcount
     * every time you create an object. */
    robj *o = createObject(REDIS_STRING, pooong); /* refcount + 1 */
    /* addReply  retains the object too. The object will be free'd
     * after addReply is done with it. */
    addReply(c, o);
    decrRefCount(o); /* refcount - 1 */
}

void *load() {
    fprintf(stderr, "%s: Loaded at %d\n", __FILE__, __LINE__);
    return NULL;
}

void cleanup(void *privdata) {
    fprintf(stderr, "%s: Cleaning up at %d\n", __FILE__, __LINE__);
}

struct redisModule redisModuleDetail = {
    REDIS_MODULE_COMMAND, /* Tell Dynamic Redis our module provides commands */
    REDIS_VERSION,        /* Provided by redis.h */
    "0.0001",             /* Version of this module (only for reporting) */
    "sh.matt.test.pong",  /* Unique name of this module */
    load,                 /* Load function pointer (optional) */
    cleanup               /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    /* See the primary command table in redis.c for details
     * about what each field means.
     * Some fields are more important than others.  You can't
     * just copy and paste an existing field into a new field
     * and expect it to work. */
    /* name, function, arity, sflags, [internal], keyFunction,
     * keyFirstPos, keyLastPos, keyStep, [internal], [internal] */
    {"poong", pingCommandLocal, 1, "rt", 0, NULL, 0, 0, 0, 0, 0},
    {"pooooong", pingCommandLocal, 1, "rt", 0, NULL, 0, 0, 0, 0, 0},
    {"pooong", pingCommandLocal, 1, "rt", 0, NULL, 0, 0, 0, 0, 0},
    {"pinger", pingCommand, 1, "rt", 0, NULL, 0, 0, 0, 0,
     0}, /* function from Redis */
    {0}  /* Always end your command table with {0}
          * If you forget, you will be reminded with a segfault on load. */
};
