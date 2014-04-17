#include "redis.h"

/* ====================================================================
 * Redis Add-on Module: scriptname
 * Provides commands: scriptname, evalname
 * Behaviors:
 *   - scriptName - bind name to a loaded script SHA (CRUD)
 *   - evalName - run a Lua script by name instead of SHA
 * ==================================================================== */

/* Global things for this module */
struct global {
    dict *names;       /* Map of name -> SHA */
    struct err {
        robj *nosha, *noname;
    } err;
};

/* ====================================================================
 * Global struct to store our persistent configuration.
 * If this wasn't a module, we'd use the global 'server' struct instead.
 * ==================================================================== */
static struct global g = {0};

/* ====================================================================
 * Dict Helpers
 * ==================================================================== */
static void *dictSdsDup(void *privdata, const void *string) {
    DICT_NOTUSED(privdata);
    return sdsnew(string);
}

/* Script name -> script sha dict */
/* Note how we assign every dup and free pointer so we don't
 * have to manually create or free objects we put in the dict. */
static dictType namesTableDictType = {
    dictSdsCaseHash,           /* hash function */
    dictSdsDup,                /* key dup */
    dictSdsDup,                /* val dup */
    dictSdsKeyCompare,         /* key compare */
    dictSdsDestructor,         /* key destructor */
    dictSdsDestructor          /* val destructor */
};

/* ====================================================================
 * Command Implementations
 * ==================================================================== */
/* scriptNameCommand() has compound sub-arguments, so it looks slightly more
 * convoluted than it actually is.  Just read each if/else branch as
 * if it were an individual command. */
void scriptNameCommand(redisClient *c) {
    char *req = c->argv[1]->ptr;
    sds script_name = c->argv[2]->ptr;

    if (c->argc == 4 && !strcasecmp(req, "set")) {
        sds target_sha = c->argv[3]->ptr;

        if (sdslen(target_sha) != 40 ||
            dictFind(server.lua_scripts,target_sha) == NULL) {
            addReply(c, g.err.nosha);
            return;
        }

        /* If name doesn't exist, dictReplace == dictAdd */
        dictReplace(g.names, script_name, target_sha);

        addReplyBulkCBuffer(c, script_name, sdslen(script_name));
    } else if (c->argc == 3 && !strcasecmp(req, "get")) {
        sds found;
        if ((found = dictFetchValue(g.names, script_name))) {
            addReplyBulkCBuffer(c, found, sdslen(found));
        } else {
            addReply(c, g.err.noname);
        }
    } else if (c->argc == 2 && !strcasecmp(req, "getall")) {
        dictIterator *di;
        dictEntry *de;

        unsigned long sz = dictSize(g.names);

        if (!sz) {
            addReply(c, shared.emptymultibulk);
            return;
        }

        /* Multiply by 2 because the size of the dict is the number of keys.
         * We are returning keys *and* values, so length is dictSize * 2 */
        addReplyMultiBulkLen(c, sz * 2);

        di = dictGetIterator(g.names);
        while ((de = dictNext(di))) {
            addReplyBulkCString(c, dictGetKey(de));
            addReplyBulkCString(c, dictGetVal(de));
        }
        dictReleaseIterator(di);
    } else if (c->argc == 3 && !strcasecmp(req, "del")) {
        sds deleted;

        if ((deleted = dictFetchValue(g.names, script_name))) {
            dictDelete(g.names, script_name);
            addReplyBulkCBuffer(c, deleted, sdslen(deleted));
        } else {
            addReply(c, g.err.noname);
        }
    } else {
        addReplyError(c, "Unknown scriptName subcommand or arg count");
    }
}

void evalName(redisClient *c, char *name) {
    sds hash;
    robj *sha;

    /* if len(name) == 40, then this *could* be a script hash.
     * Try to look it up as a hash first. */
    if (sdslen(name) == 40 && dictFind(server.lua_scripts, name)) {
        hash = name;
    } else {
        hash = dictFetchValue(g.names, name);
        /* If we didn't find it, or if we did find it but for some
         * reason the len(hash) != 40, ... */
        if (!hash || sdslen(hash) != 40) {
            addReply(c, g.err.nosha);
            return;
        }
    }

    /* the redisClient object uses robj * for fields,
     * so here we convert the sds hash into an robj of
     * an sds */
    sha = createStringObject(hash, sdslen(hash));
    rewriteClientCommandArgument(c, 1, sha);
    decrRefCount(sha); /* No leaking memory from our created robj */

    /* Now run the script as normal, just like we never existed. *poof* */
    evalGenericCommand(c, 1);
}

void evalNameCommand(redisClient *c) {
    evalName(c, c->argv[1]->ptr);
}

/* ====================================================================
 * Bring up / Teardown
 * ==================================================================== */
void *load() {
    g.names = dictCreate(&namesTableDictType, NULL);
    g.err.nosha = createObject(REDIS_STRING,sdsnew(
            "-NOSCRIPT Target SHA not found. Please use SCRIPT LOAD.\r\n"));
    g.err.noname = createObject(REDIS_STRING,sdsnew(
            "-NONAME Script name not found. Please use SCRIPTNAME SET.\r\n"));
    return NULL;
}

/* If you reload the module *without* freeing things you allocate in load(),
 * then you *will* introduce memory leaks. */
void cleanup(void *privdata) {
    /* dictRelease will free every key, every value, then the dict itself. */
    dictRelease(g.names);
    decrRefCount(g.err.nosha);
    decrRefCount(g.err.noname);
}

/* ====================================================================
 * Dynamic Redis API Requirements
 * ==================================================================== */
struct redisModule redisModuleDetail = {
   REDIS_MODULE_COMMAND, /* Tell Dynamic Redis our module provides commands */
   REDIS_VERSION,        /* Provided by redis.h */
   "0.3",                /* Version of this module (only for reporting) */
   "sh.matt.scriptName", /* Unique name of this module */
   load,                 /* Load function pointer (optional) */
   cleanup               /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    {"scriptName",scriptNameCommand,-2,"s",0,NULL,0,0,0,0,0},
#if DYN_REDIS_VER == 1000501
    {"evalName",evalNameCommand,-3,"s",0,NULL,0,0,0,0,0},
#else
    /* evalGetKeys exists for Cluster in redis-unstable */
    {"evalName",evalNameCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
#endif
    {0}  /* Always end your command table with {0}
          * If you forget, you will be reminded with a segfault on load. */
};
