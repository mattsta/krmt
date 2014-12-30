#include "json.h"
#include "jsonobj.h"
#include "jsonobj_box.h"
#include "jsonobj_get.h"
#include "jsonobj_set.h"
#include "jsonobj_delete.h"
#include "i_yajl.h"

/* ====================================================================
 * Redis Add-on Module: json
 * Provides commands: hgetalljson, hmsetjson
 * Behaviors:
 *   - hgetalljson - same as hgetall, but results returned as JSON map
 *   - jsonwrap - wrap any Redis command in JSON a key-value map result
 *   - jsondocvalidate - check if your JSON validates using Redis rules
 *   - jsondocset - add (or overwrite) JSON document by key
 *   - jsondocget - get JSON document with existing key
 *   - jsondocmget - get multiple JSON documents at once
 *   - jsondocdel - remove JSON documents by key
 *   - jsondocsetbyjson - add (or overwrite) JSON documents by providing a
 *                        JSON array of maps, each with a field usable for IDs
 *   - jsondockeys - get all keys (fields) of a JSON document
 *   - jsonfieldget - get the value of one field of a JSON document
 *   - jsonfieldset - set the value of one field of a JSON document [NI]
 *   - jsonfielddel - remove a field from a JSON document or sub-container
 *   - jsonfieldincrby - increment-by-int a number stored in a map or array
 *   - jsonfieldincrbyfloat - increment-by-float number stored in map or array
 *   - jsonfieldrpushx - push a number or string into a homogeneous list
 *   - jsonfieldrpop - remove and return the last element of a list
 * ==================================================================== */

struct global g = {0};

/* ====================================================================
 * Redis Internal Client Management
 * ==================================================================== */
static redisClient *newFakeClient() {
    redisClient *fake_client = createClient(-1);
    /* The fake client buffer only works if fd == -1 && flags & LUA_CLIENT */
    fake_client->flags |= REDIS_LUA_CLIENT;
    return fake_client;
}

/* Force fake clients to use the same DB as the caller */
static void jsonSyncClients(redisClient *c) {
    g.c_noreturn->db = g.c->db = c->db;
}

robj *dbstrTake(sds id) { return createObject(REDIS_STRING, id); }
robj *dbstr(sds id) { return dbstrTake(sdsdup(id)); }

void clearClient(redisClient *c) {
    /* can't auto-free argv here because some argv are just
     * pointers to auto arrays */
    c->argc = 0;
    c->argv = NULL;
}

void freeFakeClientResultBuffer(redisClient *c, sds reply) {
    /* If the reply isn't our reply buffer, we created it
     * independently and need to free it here. */
    /* Else, it still belongs to the client to deal with. */
    if (reply != c->buf)
        sdsfree(reply);
}

/* result buffer aggregation is taken from scripting.c */
sds fakeClientResultBuffer(redisClient *c) {
    sds reply;

    if (listLength(c->reply) == 0 && c->bufpos < REDIS_REPLY_CHUNK_BYTES) {
        /* This is a fast path for the common case of a reply inside the
         * client static buffer. Don't create an SDS string but just use
         * the client buffer directly. */
        c->buf[c->bufpos] = '\0';
        reply = c->buf;
        c->bufpos = 0;
    } else {
        reply = sdsnewlen(c->buf, c->bufpos);
        c->bufpos = 0;
        while (listLength(c->reply)) {
            robj *o = listNodeValue(listFirst(c->reply));

            reply = sdscatlen(reply, o->ptr, sdslen(o->ptr));
            listDelNode(c->reply, listFirst(c->reply));
        }
    }

    return reply;
}

/* Don't let people use keys starting with a valid container box value */
bool validateKeyFormatAndReply(redisClient *c, sds key) {
    if (isValidBoxKeyPrefix(key[0])) {
        addReplyErrorFormat(
            c, "Invalid first character in key: [%c] (binary [%s])", key[0],
            bb(key[0]));
        return false;
    }

    return true;
}

/* ====================================================================
 * Field Key Helpers
 * ==================================================================== */
/* Join the first (argc - trim) arguments into a colon key */
static sds genFieldAccessor(redisClient *c, int trim) {
    int usable = c->argc - (1 + trim);
    sds parts[usable];

    for (int i = 1; i < usable + 1; i++) {
        parts[i - 1] = c->argv[i]->ptr;
    }

    return sdsjoin(parts, usable, ":");
}

/* Search for 'field_name' as the name of a field in o.
 * Returns the value for 'field_name' in o wrapped in an (robj *). */
static robj *jsonObjFindId(sds field_name, struct jsonObj *o) {
    /* Only search for IDs in maps */
    switch (o->type) {
    case JSON_TYPE_MAP:
        break;
    default:
        return NULL;
    }

    /* for each field in the map, search for field_name and return
     * an robj of the value for that field. */
    for (int i = 0; i < o->content.obj.elements; i++) {
        struct jsonObj *f = o->content.obj.fields[i];
        if (!strcmp(f->name, field_name)) {
            switch (f->type) {
            case JSON_TYPE_STRING:
            case JSON_TYPE_NUMBER_AS_STRING:
                D("Setting found id to: %s\n", f->content.string);
                return dbstr(f->content.string);
                break;
            default:
                /* We can't set a container type or immediate type
                 * (direct number/true/false/null) as a key */
                return NULL;
            }
        }
    }
    return NULL;
}

/* Delete existing document then add new document */
static int jsonObjAddToDB(robj *key, struct jsonObj *root) {
    int deleted;
    switch (root->type) {
    case JSON_TYPE_MAP:
        /* Only add map types! */
        deleted = hgetallAndRecursivelyDeleteEverything(key->ptr);
        jsonObjToRedisDB(key, root);
        return deleted;
        break;
    default:
        return 1;
        break;
    }
}

/* ====================================================================
 * Command Implementations
 * ==================================================================== */
/* jsonwrap assumes all commands return pairs of K V strings in a multibulk */
void jsonwrapCommand(redisClient *c) {
    /* args 0-N: ["jsonwrap", targetCommand, arg1, arg2, ..., argn] */
    jsonSyncClients(c);
    genericWrapCommandAndReply(c);
}

/* More efficient version of jsonwrap for hgetall only */
void hgetalljsonCommand(redisClient *c) {
    addReplyMultiBulkLen(c, c->argc - 1);

    jsonSyncClients(c);
    for (int i = 1; i < c->argc; i++) {
        hgetallToJsonAndAndReply(c, c->argv[i]->ptr);
    }
}

void jsondocsetCommand(redisClient *c) {
    /* args 0-N: ["jsondocset", id, json, [id2, json2, ...]] */
    /* If only K:V strings, submit as one hmset command */

    /* This function is O(3*N) in the number of total keys submitted:
     *   - first, we validate the keys
     *   - second, we process all the json
     *   - third, we add all the JSON to the DB.
     *   + we run each loop indepdently because we must be able to abort
     *     the entire add sequence if any prior aggregate attempt fails
     *     (e.g. if you have a bad key or bad json, we don't want to add
     *           _any_ of these documents.) */

    /* If we don't have an even number of Key/Document pairs, exit. */
    if ((c->argc - 1) % 2 != 0) {
        addReplyError(
            c, "Invalid number of arguments.  Must match keys to documents.");
        return;
    }

    /* If any of the keys are invalid, exit. */
    for (int i = 1; i < c->argc; i += 2) {
        if (!validateKeyFormatAndReply(c, c->argv[i]->ptr))
            return;
    }

    int documents = (c->argc - 1) / 2;

    struct jsonObj *additions[documents];
    for (int i = 1; i < c->argc; i += 2) {
        struct jsonObj *root = yajl_decode(c->argv[i + 1]->ptr, NULL);

        additions[i / 2] = root;

        if (!root) {
            /* Free any jsonObj trees already created */
            for (int j = 0; j < i; j++)
                jsonObjFree(additions[j]);

            addReplyErrorFormat(c, "Invalid JSON at document %d.  Use "
                                   "JSONDOCVALIDATE [json] for complete error.",
                                i / 2);
            return;
        }
    }

    jsonSyncClients(c);
    int deleted = 0;
    /* Now jump keys at positions i*2 and documents at i/2 */
    for (int i = 1; i < c->argc; i += 2) {
        struct jsonObj *root = additions[i / 2];
        /* We can add a much simpler "check if key exists" test here first: */
        deleted += jsonObjAddToDB(c->argv[i], root);
        jsonObjFree(root);
        D("Parsed Json %d!\n", i);
    }

    /* Reply += 1 if the document is new; reply += 0 if the document updated
     * (where an update is a full Delete/Create cycle) */
    /* Replies with number of new keys set.  Existing keys also get
     * set, but don't count as new. */
    addReplyLongLong(c, documents - deleted);
}

void jsondocvalidateCommand(redisClient *c) {
    /* args 0-1: ["jsondocvalidate", json] */

    sds err = sdsempty();
    struct jsonObj *root = yajl_decode(c->argv[1]->ptr, &err);

    if (!root) {
        int sz;
        sds *error_lines = sdssplitlen(err, sdslen(err), "\n", 1, &sz);
        addReplyMultiBulkLen(c, sz + 1);
        addReply(c, g.err_parse);
        for (int i = 0; i < sz; i++) {
            addReplyBulkCBuffer(c, error_lines[i], sdslen(error_lines[i]));
        }
        sdsfreesplitres(error_lines, sz);
    } else {
        addReply(c, shared.ok);
    }
    sdsfree(err);
}

/* Returns ID(s) of documents added */
void jsondocsetbyjsonCommand(redisClient *c) {
    /* args 0-N: ["jsondocsetbyjson", id-field-name, json] */
    jsonSyncClients(c);

    sds field_name = c->argv[1]->ptr;
    struct jsonObj *root = yajl_decode(c->argv[2]->ptr, NULL);

    if (!root) {
        addReply(c, g.err_parse);
        return;
    }

    robj *key;
    if (root->type == JSON_TYPE_LIST) {
        robj *keys[root->content.obj.elements];
        D("Procesing list!\n");
        /* First, process all documents for names to make sure they are valid
         * documents.  We don't want to add half the documents then reach a
         * failure scenario. */
        for (int i = 0; i < root->content.obj.elements; i++) {
            struct jsonObj *o = root->content.obj.fields[i];
            key = jsonObjFindId(field_name, o);
            keys[i] = key;
            if (!key) {
                /* Free any allocated keys so far */
                for (int j = 0; i < j; j++)
                    decrRefCount(keys[j]);

                jsonObjFree(root);
                addReplyErrorFormat(
                    c,
                    "field '%s' not found or unusable as key for document %d",
                    field_name, i);
                return;
            }
        }
        /* Now actually add all the documents */
        /* Note how a multi-set gets a multibulk reply while
         * a regular one-document set gets just one bulk result. */
        addReplyMultiBulkLen(c, root->content.obj.elements);
        for (int i = 0; i < root->content.obj.elements; i++) {
            struct jsonObj *o = root->content.obj.fields[i];
            jsonObjAddToDB(keys[i], o);
            addReplyBulkCBuffer(c, keys[i]->ptr, sdslen(keys[i]->ptr));
            decrRefCount(keys[i]);
        }
    } else if (root->type == JSON_TYPE_MAP) {
        key = jsonObjFindId(field_name, root);
        if (key) {
            jsonObjAddToDB(key, root);
            addReplyBulkCBuffer(c, key->ptr, sdslen(key->ptr));
            decrRefCount(key);
        } else {
            addReplyErrorFormat(
                c, "field '%s' not found or unusable as key for document",
                field_name);
        }
    } else {
        addReplyError(c, "JSON isn't map or array of maps.");
    }

    jsonObjFree(root);
}

void jsondocdelCommand(redisClient *c) {
    /* args 0-N: ["jsondocdel", id1, id2, ..., idN] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    long deleted = 0;
    for (int i = 1; i < c->argc; i++) {
        deleted += hgetallAndRecursivelyDeleteEverything(c->argv[i]->ptr);
    }
    addReplyLongLong(c, deleted);
}

/* a simple HKEYS on the proper hash for the requested document */
void jsondockeysCommand(redisClient *c) {
    /* args 0-N: ["jsondockeys", json-doc-id, [sub-id-1, sub-id-2, ...]] */
    jsonSyncClients(c);

    sds key = genFieldAccessor(c, 0);

    sds found;
    int type;
    findKeyForHash(key, &found, &type);
    if (!found || type != JSON_TYPE_MAP) {
        addReplyError(c, "Key isn't a JSON document");
        return;
    }

    decrRefCount(c->argv[1]);
    c->argv[1] = dbstrTake(found);
    sdsfree(key);
    hkeysCommand(c);
}

void jsondocgetCommand(redisClient *c) {
    /* args 0-N: ["jsondocget", jsonkey] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    findJsonAndReply(c, c->argv[1]->ptr);
}

void jsondocmgetCommand(redisClient *c) {
    /* args 0-N: ["jsondocmget", jsonkey1, jsonkey2, ..., jsonkeyN] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    addReplyMultiBulkLen(c, c->argc - 1);
    for (int i = 1; i < c->argc; i++) {
        findJsonAndReply(c, c->argv[i]->ptr);
    }
}

void jsonfieldgetCommand(redisClient *c) {
    /* args 0-N: ["jsonfieldget", json-dotted-key1, ..., json-dotted-keyN] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    sds key = genFieldAccessor(c, 1);
    sds field = c->argv[c->argc - 1]->ptr;

    D("Asking for Key [%s] and Field [%s]\n", key, field);
    findJsonFieldAndReply(c, key, field);

    sdsfree(key);
}

void jsonfieldsetCommand(redisClient *c) {
    /* args 0-N: ["jsonfieldset", json field components, new-json] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    /* sds key = genFieldAccessor(c, 1); */
    addReplyBulkCString(c, "Thanks for your interest in the JSONFIELDSET "
                           "command, but it is currently not implemented.");
}

void jsonfieldincrbyGeneric(redisClient *c, bool use_incrbyfloat) {
    /* args 0-N: ["jsonfieldincrby", json field components, incrby number] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    sds key = genFieldAccessor(c, 2);
    sds found;
    int type;
    int decode_as = findKeyForHash(key, &found, &type);
    sdsfree(key);

    if (!found) {
        addReplyError(c, "JSON document not found");
        return;
    } else if (decode_as == DECODE_INDIVIDUAL) {
        /* 'field' is the second to last argv[] element */
        struct jsonObj *f =
            hgetToJsonObj(found, decode_as, c->argv[c->argc - 2]->ptr);
        switch (f->type) {
        case JSON_TYPE_MAP:
        case JSON_TYPE_LIST:
        case JSON_TYPE_TRUE:
        case JSON_TYPE_FALSE:
        case JSON_TYPE_NULL:
            addReplyError(c, "Requested field not usable for incrby. Can't "
                             "increment non-number types.");
            break;
        case JSON_TYPE_NUMBER:
        /* found->content.number += incrby;
        break; */
        case JSON_TYPE_STRING:
        case JSON_TYPE_NUMBER_AS_STRING:
            /* strtoi -> += incrby -> store again */
            addReplyError(c,
                          "Not currently supported on mixed-type containers.");
            break;
        }
        jsonObjFree(f);
        return;
    }

    /* Target args: 0-3: [_, HASH, FIELD, INCRBY] */
    /* The hincrby* commands don't check argc, so we don't care if
     * we have extra arguments after INCRBY.  They'll get cleaned up
     * when the client exits. */
    decrRefCount(c->argv[1]);
    c->argv[1] = dbstrTake(found);

    /* If argc == 4, then the second and third arguments are already okay.
     * If argc  > 4, we move the last two arguments to positions 3-4 */
    if (c->argc > 4) {
        decrRefCount(c->argv[2]);          /* goodbye, original argv[2] */
        c->argv[2] = c->argv[c->argc - 2]; /* field is 2nd to last argument */
        c->argv[c->argc - 2] = NULL;

        /* If argc == 5, then argv[3] is the one we moved to argv[2] above.
         * We can't release it because it just moved storage locations */
        if (c->argc > 5)
            decrRefCount(c->argv[3]);      /* goodbye, original argv[3] */
        c->argv[3] = c->argv[c->argc - 1]; /* incrby value is last argument */
        c->argv[c->argc - 1] = NULL;
        c->argc -= c->argc > 5 ? 2 : 1; /* if > 5, we removed two. else, we
                                         * removed one and moved the other. */
    }

    if (use_incrbyfloat)
        hincrbyfloatCommand(c);
    else
        hincrbyCommand(c);
}

void jsonfieldincrbyCommand(redisClient *c) {
    jsonfieldincrbyGeneric(c, false);
}

void jsonfieldincrbyfloatCommand(redisClient *c) {
    jsonfieldincrbyGeneric(c, true);
}

void jsonfieldrpushxCommand(redisClient *c) {
    /* args 0-N: ["jsonfieldrpushx", key, sub-key1, sub-key2, ..., new json] */
    jsonSyncClients(c);
    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    sds key = genFieldAccessor(c, 1);
    sds found;
    int type;
    int decode_as = findKeyForList(key, &found, &type);
    sdsfree(key);

    if (!found) {
        addReplyError(c, "JSON List not found");
        return;
    }

    /* json is last argument */
    struct jsonObj *o = yajl_decode(c->argv[c->argc - 1]->ptr, NULL);
    if (!o) {
        addReply(c, g.err_parse);
        sdsfree(found);
        return;
    } else if (o->type == JSON_TYPE_MAP || o->type == JSON_TYPE_LIST) {
        /* Implementation outline:
         *   - get length of parent list
         *   - create new container type with key sdsAppendColon(key, LENGTH+1)
         *   - collapse container type to box.
         *   - rpushx box into the list. */
        addReplyError(c, "Sorry, you can only add basic types (string, number, "
                         "true/false/null) to a list until somebody finishes "
                         "this feature.");
        jsonObjFree(o);
        sdsfree(found);
        return;
    } else if (decode_as == DECODE_ALL_NUMBER &&
               (o->type != JSON_TYPE_NUMBER &&
                o->type != JSON_TYPE_NUMBER_AS_STRING)) {
        /* Complete implementation outline:
         *   - convert homogeneous number list to individual list by:
         *   - locate parent of list, rename box from JLIST|JHOMOGENEOUS|JNUMBER
         * to just JLIST
         *   - rename this list to 'key' instead of 'found'
         *   - For each current element of the list, box as number.
         *   - Now you can add your new non-number type to the list. */
        addReplyError(c, "You must add only numbers to your number-only list.");
        jsonObjFree(o);
        sdsfree(found);
        return;
    } else if (decode_as == JSON_TYPE_STRING && (o->type != JSON_TYPE_STRING)) {
        /* Complete implementation outline:
         *   - see details for number above, but replace with string. */
        addReplyError(c, "You must add only strings to your string-only list.");
        jsonObjFree(o);
        sdsfree(found);
        return;
    } else if (decode_as == DECODE_INDIVIDUAL) {
        jsonObjBoxBasicType(o);
    }

    /* Remove entire client argument list after the command */
    for (int i = 1; i < c->argc; i++)
        decrRefCount(c->argv[i]);

    c->argc = 3;
    /* Re-populate our argument list */
    /* We are reusing the origina c->argv memory, but it'll be
     * free'd when the client exists.  We are guarnateed to have
     * at least four argument pointers available to us, and we only
     * need to have three allocated.  Perfecto. */
    c->argv[1] = dbstrTake(found);
    c->argv[2] = dbstrTake(o->content.string);
    o->content.string = NULL;
    jsonObjFree(o);

    /* Target args: 0-3: [_, LIST, RAW-STR-OR-NUMBER] */
    rpushxCommand(c);
}

void jsonfieldrpopCommand(redisClient *c) {
    /* args 0-N: ["jsonfielddel", key, sub-key1, sub-key2, ...] */
    jsonSyncClients(c);
    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    sds key = genFieldAccessor(c, 0);
    sds found;
    int type;
    int decode_as = findKeyForList(key, &found, &type);

    if (!found) {
        addReplyError(c, "JSON List not found");
        return;
    }

    rpopRecursiveAndReply(c, found, decode_as);
    sdsfree(found);
    sdsfree(key);
}

void jsonfielddelCommand(redisClient *c) {
    /* args 0-N: ["jsonfielddel", key, sub-key1, sub-key2, ..., field] */
    jsonSyncClients(c);

    if (!validateKeyFormatAndReply(c, c->argv[1]->ptr))
        return;

    sds key = genFieldAccessor(c, 1);

    long deleted = findAndRecursivelyDelete(key, c->argv[c->argc - 1]->ptr);

    addReplyLongLong(c, deleted);
    sdsfree(key);
}

/* ====================================================================
 * Bring up / Teardown
 * ==================================================================== */
void *load() {
    g.c = newFakeClient();
    g.c_noreturn = createClient(-1);
    g.err_parse = createObject(
        REDIS_STRING,
        sdsnew("-ERR JSON Parse Error.  You can debug with JSONDOCVALIDATE."));
    return NULL;
}

/* If you reload the module *without* freeing things you allocate in load(),
 * then you *will* introduce memory leaks. */
void cleanup(void *privdata) {
    freeClient(g.c);
    freeClient(g.c_noreturn);
    decrRefCount(g.err_parse);
}

/* ====================================================================
 * Dynamic Redis API Requirements
 * ==================================================================== */
struct redisModule redisModuleDetail = {
    REDIS_MODULE_COMMAND, /* Tell Dynamic Redis our module provides commands */
    REDIS_VERSION,        /* Provided by redis.h */
    "0.3",                /* Version of this module (only for reporting) */
    "sh.matt.json",       /* Unique name of this module */
    load,                 /* Load function pointer (optional) */
    cleanup               /* Cleanup function pointer (optional) */
};

struct redisCommand redisCommandTable[] = {
    {"hgetalljson", hgetalljsonCommand, -2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsonwrap", jsonwrapCommand, -2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsondocvalidate", jsondocvalidateCommand, 2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsondocset", jsondocsetCommand, -3, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsondocget", jsondocgetCommand, 2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsondocmget", jsondocmgetCommand, -2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsondocdel", jsondocdelCommand, -2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsondocsetbyjson", jsondocsetbyjsonCommand, -2, "r", 0, NULL, 0, 0, 0, 0,
     0},
    {"jsondockeys", jsondockeysCommand, -2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsonfieldget", jsonfieldgetCommand, -3, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsonfieldset", jsonfieldsetCommand, 3, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsonfielddel", jsonfielddelCommand, -2, "r", 0, NULL, 0, 0, 0, 0, 0},
    {"jsonfieldincrby", jsonfieldincrbyCommand, -3, "r", 0, NULL, 0, 0, 0, 0,
     0},
    {"jsonfieldincrbyfloat", jsonfieldincrbyfloatCommand, -3, "r", 0, NULL, 0,
     0, 0, 0, 0},
    {"jsonfieldrpushx", jsonfieldrpushxCommand, -4, "r", 0, NULL, 0, 0, 0, 0,
     0},
    {"jsonfieldrpop", jsonfieldrpopCommand, -3, "r", 0, NULL, 0, 0, 0, 0, 0},
    {0} /* Always end your command table with {0}
         * If you forget, you will be reminded with a segfault on load. */
};
