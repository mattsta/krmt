#include "jsonobj_get.h"

static struct jsonObj *jsonObjFromRedisListSdsKey(sds key, int decode_as);
static struct jsonObj *jsonObjFromRedisListAtIndexSdsKey(sds key, int decode_as,
                                                         int idx);

/* ====================================================================
 * Create Returnable jsonObj from Redis Output Buffer
 * ==================================================================== */
static struct jsonObj *jsonObjCreateMapFromBuffer(char *buf,
                                                  int buffer_elements, sds key,
                                                  int decode_as) {
    sds name = NULL;
    char *next = buf;
    struct jsonObj *result = jsonObjCreateMap();
    sds populate_as = NULL;

    /* If buffer_elements is odd, we ignore the last element. */
    /* (Otherise, we end up with a "name" allocated, but it never
     * joins gets tied to an object, so it's a memory leak. */
    if (buffer_elements % 2 != 0)
        buffer_elements--;

    /* Elements have [TYPEID][SIZE][CRLF][CONTENT][CRLF] */
    for (int i = 0; i < buffer_elements; i++) {
#if 0
        /* If this is a nested multi-bulk type, then generate a sub-object */
        if (buf[0] == '*') {
            int sz = strtol(buf+1, &next, 10);
            buf = next += SZ_CRLF;
            struct jsonObj *f = jsonObjCreateMapFromBuffer(buf, sz, key, decode_as);
            /* Note: we *should* update 'buf' here to point to the last byte
             * used in the nested decode, but we don't have that returned or
             * exposed anywhere yet. */
            jsonObjTakeName(f, sdsfromlonglong(i));
            jsonObjAddField(result, f);
            continue;
        }
#endif

        /* Ignore any sub multi-bulk indicators and treat everything as a flat
         * return map. */
        if (buf[0] == '*') {
            /* Jump over count and CRLF of count up to next '$' */
            int sz = strtol(buf+1, &next, 10);
            buf = next + SZ_CRLF;
            /* We need to iterate over more elements, so increase elements to
             * compensate.  (Note, we already have "1" for this run, so add
             * (sz - 1) to elements. */
            buffer_elements += sz-1;
        }

        if (buf[0] != '$')
            D("Non bulk type in multibulk!  Can't KV this: %s.\n", buf);
        else
            buf++; /* skip over '$' */

        int sz = strtol(buf, &next, 10);
        next += SZ_CRLF;

        sds bulk_data;
        bulk_data = sdsnewlen(next, sz);
        if (i % 2 == 0) {
            name = bulk_data;
        } else {
            struct jsonObj *f;
            switch (decode_as) {
            case DECODE_ALL_NUMBER:
                f = jsonObjNumberAsStringTake(bulk_data);
                break;
            case DECODE_ALL_STRING:
                f = jsonObjStringTake(bulk_data);
                break;
            case DECODE_INDIVIDUAL:
                populate_as = sdsAppendColon(key, name);
                f = jsonObjFromBoxedBuffer(bulk_data, populate_as);
                sdsfree(populate_as);
                break;
            default:
                D("BAD DECODE: %d\n", decode_as);
                f = NULL;
                break;
            }
            jsonObjTakeName(f, name);
            jsonObjAddField(result, f);
            f = NULL;
        }
        next += sz + SZ_CRLF;
        buf = next;
    }
    return result;
}

/* Process a HGETALL response recursively until all sub-containers have been
 * populated too. */
static struct jsonObj *redisKVProtocolToJSONContainer(sds reply, sds key,
                                                      int decode_as) {
    char *p = reply;
    char *after = reply + 1;
    char *next = NULL;
    int sz = strtol(after, &next, 10);
    next += SZ_CRLF; /* jump over \r\n */
    sds snext;       /* We must reallocate next as sds if it's passed along */

    if (sz < 0)
        return NULL;

    struct jsonObj *o = NULL;
    /* The first three are immediate data after their [TYPEID] */
    /* The last two have [TYPEID][SIZE][CRLF] */
    switch (*p) {
    case '+': /* Status */
        return jsonObjStringLen(after, strlen(after) - SZ_CRLF);
        break;
    case '-': /* Error */
        return jsonObjStringLen(reply, strlen(reply) - SZ_CRLF);
        break;
    case ':': /* Integer */
        return jsonObjNumber(sz);
        break;
    case '$': /* Bulk */
        snext = sdsnewlen(next, sz);
        switch (decode_as) {
        case DECODE_ALL_STRING:
            o = jsonObjStringTake(snext);
            break;
        case DECODE_ALL_NUMBER:
            o = jsonObjNumberAsStringTake(snext);
            break;
        case DECODE_INDIVIDUAL:
            o = jsonObjFromBoxedBuffer(snext, key);
            break;
        default:
            sdsfree(snext);
            break;
        }
        return o;
        break;
    case '*': /* Multi Bulk */
        /* We're safe passing the raw next offset here */
        o = jsonObjCreateMapFromBuffer(next, sz, key, decode_as);
        return o;
        break;
    }
    D("returning NULL because no type match\n");
    return NULL;
}

/* Run HGETALL and bundle the response into a jsonObj */
static struct jsonObj *hgetallToJsonObj(sds key, int decode_as) {
    redisClient *fake_client = g.c;

    fake_client->argc = 2;
    robj *argv[2] = { 0 };

    robj *proper_key = dbstr(key);
    argv[1] = proper_key;

    fake_client->argv = argv;
    hgetallCommand(fake_client);
    clearClient(fake_client);

    /* Coalesce client buffer into a single string we can iterate over */
    sds fake_client_buffer = fakeClientResultBuffer(fake_client);

    /* Turn the fake client buffer into a JSON object tree */
    struct jsonObj *f = redisKVProtocolToJSONContainer(
        fake_client_buffer, proper_key->ptr, decode_as);
    sdsfree(fake_client_buffer);
    decrRefCount(proper_key);

    return f;
}

/* Run HGET for a specific field */
struct jsonObj *hgetToJsonObj(sds key, int decode_as, sds field) {
    redisClient *fake_client = g.c;

    fake_client->argc = 3;
    robj *argv[3] = { 0 };

    D("Reading key: [%s] with field: [%s]\n", key, field);
    robj *proper_key = dbstr(key);
    robj *proper_field = dbstr(field);
    argv[1] = proper_key;
    argv[2] = proper_field;

    fake_client->argv = argv;
    hgetCommand(fake_client);

    clearClient(fake_client);

    /* Coalesce client buffer into a single string we can iterate over */
    sds fake_client_buffer = fakeClientResultBuffer(fake_client);

    /* Turn the fake client buffer into a JSON object tree */

    sds populate_as = sdsAppendColon(key, field);
    struct jsonObj *f = redisKVProtocolToJSONContainer(fake_client_buffer,
                                                       populate_as, decode_as);
    sdsfree(populate_as);
    sdsfree(fake_client_buffer);
    decrRefCount(proper_key);
    decrRefCount(proper_field);

    return f;
}

/* Given a requested key, discover if it actually exists within
 * a homogeneous type box, then run HGETALL on the proper key. */
static struct jsonObj *hgetallToJsonObjFindHash(sds key) {
    /* We don't know if the top level will be [BOX]KEY or KEY, so try
     * container homogeneous boxes first and return the best one. */
    sds found;
    int type;
    int decode_as = findKeyForHash(key, &found, &type);

    if (!found || type == JSON_TYPE_LIST)
        return NULL;

    D("Found hash: [%s] from original key: [%s], decode as: %d\n", found, key,
      decode_as);

    struct jsonObj *f = hgetallToJsonObj(found, decode_as);
    sdsfree(found);
    return f;
}

/* Given a requested container key and field name (or position),
 * return the value of the field. */
static struct jsonObj *hgetToJsonObjFindHash(sds key, sds field) {
    /* We don't know if the top level will be [BOX]KEY or KEY, so try all
     * the container homogeneous boxes and return the best one. */
    sds found = NULL;
    int type;
    struct jsonObj *f = NULL;
    int decode_as = findKeyForHash(key, &found, &type);

    if (found && type == JSON_TYPE_MAP) {
        /* we found a hash; use HGET */
        D("FOUND HASH for key [%s], retrieving using [%s]\n", key, found);
        f = hgetToJsonObj(found, decode_as, field);
    } else {
        /* If we get here and found exists, we have a list from above.
         * If we get here and found doesn't exist, we have to search for
         * a boxed list type. */
        if (!found) {
            /* else, look for a list */
            decode_as = findKeyForList(key, &found, &type);

            /* List not found either.  Return nothing. */
            if (!found)
                return NULL;
        }

        /* We found a list (somehow) */
        D("FOUND LIST for key [%s], retrieving using [%s]\n", key, found);
        f = jsonObjFromRedisListAtIndexSdsKey(found, decode_as, atoi(field));
    }
    sdsfree(found);
    return f;
}

/* Wrapper to call the JSON encoder */
static sds jsonObjToJson(struct jsonObj *f) { return yajl_encode(f); }

/* Very basic HGETALL to JSON converter */
void hgetallToJsonAndAndReply(redisClient *c, sds key) {
    struct jsonObj *f = hgetallToJsonObj(key, DECODE_ALL_STRING);

    sds json = jsonObjToJson(f);
    jsonObjFree(f);

    addReplyBulkCBuffer(c, json, sdslen(json));
    sdsfree(json);
}

/* Read the output buffer of fake_client, generate JSON from
 * the retrieved field/value pairs, and return JSON to the client */
static void redisClientBufferToJsonAndReply(redisClient *c,
                                            redisClient *fake_client) {
    /* Coalesce client buffer into a single string we can iterate over */
    sds fake_client_buffer = fakeClientResultBuffer(fake_client);

    /* Turn the fake client buffer into a JSON object tree */
    struct jsonObj *f = redisKVProtocolToJSONContainer(fake_client_buffer, NULL,
                                                       DECODE_ALL_STRING);
    sdsfree(fake_client_buffer);

    /* Turn the JSON object tree into JSON */
    sds json = jsonObjToJson(f);
    jsonObjFree(f);

    addReplyBulkCBuffer(c, json, sdslen(json));
    sdsfree(json);
}

/* Given a start key, find the actual (potentially boxed) key, turn
 * the target of the key into JSON, and return the JSON to c */
void findJsonAndReply(redisClient *c, sds search_key) {
    struct jsonObj *f = hgetallToJsonObjFindHash(search_key);

    if (!f) {
        addReply(c, shared.nullbulk);
        return;
    }

    /* Turn the JSON object tree into JSON */
    sds json = jsonObjToJson(f);
    jsonObjFree(f);

    addReplyBulkCBuffer(c, json, sdslen(json));
    sdsfree(json);
}

/* Given a start key, find the actual (potentially boxed) key, turn
 * the target of the key into JSON, and return the JSON to c */
void findJsonFieldAndReply(redisClient *c, sds container_key, sds field) {
    /* detect type based on contianer key, don't assume hash up front.
     * if it's a list, turn field into an integer offset for the list */
    struct jsonObj *f = hgetToJsonObjFindHash(container_key, field);

    if (!f) {
        addReply(c, shared.nullbulk);
        return;
    }

    /* Turn the JSON object tree into JSON */
    sds json = jsonObjToJson(f);
    jsonObjFree(f);

    addReplyBulkCBuffer(c, json, sdslen(json));
    sdsfree(json);
}

/* Wrap any Redis command in a JSON K->V extractor.  Does no type
 * conversion, so all returned values are strings.  If the command
 * returns non-multiple-of-two multibulk results, the last result is
 * dropped since it has no matching value. */
void genericWrapCommandAndReply(redisClient *c) {
    struct redisCommand *cmd = lookupCommand(c->argv[1]->ptr);

    if (!cmd) {
        addReplyError(c, "requested command to wrap not found");
        return;
    }

    int cmd_argc = c->argc - 1;
    if ((cmd->arity > 0 && cmd->arity != cmd_argc) ||
        (cmd_argc < -cmd->arity)) {
        addReplyError(
            c, "requested command to wrap has wrong number of arguments");
        return;
    }

    redisClient *fake_client = g.c;

    /* Create fake argv (it's free'd by freeClient()) */
    fake_client->argc = cmd_argc;
    robj *argv[cmd_argc];

    /* Note: we're borrowing these (robj *) for a while.  We don't increment
    the refcount, but we don't release them either. */
    for (int i = 1; i < c->argc; i++) {
        argv[i - 1] = c->argv[i];
    }

    fake_client->argv = argv;

    cmd->proc(fake_client);
    clearClient(fake_client);

    redisClientBufferToJsonAndReply(c, fake_client);
}

/* Given the [BOX] of a value, determine how to either:
 *   - unpack the next sub-container (map/list)
 *   - decode the value in-place (string/number/true/false/null) */
/* jsonObjFromBox is responsible for freeing 'buffer' or using it
 * elsewhere as necessary. */
static struct jsonObj *jsonObjFromBox(unsigned char box, sds buffer,
                                      sds populate_as) {
    struct jsonObj *o = NULL;
    sds boxed;

    switch (openBoxAction(box)) {
    case BOX_FETCH_MAP_DECODE:
        o = hgetallToJsonObj(populate_as, DECODE_INDIVIDUAL);
        break;
    case BOX_FETCH_MAP_NUMBER:
        boxed = boxgen(box, populate_as);
        o = hgetallToJsonObj(boxed, DECODE_ALL_NUMBER);
        sdsfree(boxed);
        break;
    case BOX_FETCH_MAP_STRING:
        boxed = boxgen(box, populate_as);
        o = hgetallToJsonObj(boxed, DECODE_ALL_STRING);
        sdsfree(boxed);
        break;
    case BOX_FETCH_LIST_DECODE:
        o = jsonObjFromRedisListSdsKey(populate_as, DECODE_INDIVIDUAL);
        break;
    case BOX_FETCH_LIST_NUMBER:
        o = jsonObjFromRedisListSdsKey(populate_as, DECODE_ALL_NUMBER);
        break;
    case BOX_FETCH_LIST_STRING:
        o = jsonObjFromRedisListSdsKey(populate_as, DECODE_ALL_STRING);
        break;
    case BOX_PARSE_NUMBER_AFTER_BOX:
        o = jsonObjNumberAsStringTake(buffer);
        buffer = NULL;
        break;
    case BOX_PARSE_STRING_AFTER_BOX:
        o = jsonObjStringTake(buffer);
        buffer = NULL;
        break;
    case BOX_DECODE_IMMEDIATE:
        o = jsonObjCreate();
        o->type = jsonObjBasicTypeFromBox(box);
        break;
    default:
        D("ERR: Unknown boxaction: (Boxtype: [%s], boxaction: [%d]\n", bb(box),
          openBoxAction(box));
        break;
    }
    sdsfree(buffer);
    return o;
}

/* Given a boxed Redis value, use the box to retrieve the proper
 * JSON value we're going to return. */
/* NOTE: 'buffer' is now owned by this function and is modified along
 * the way.  If you need to keep your original value, send BoxedBuffer
 * a copy. */
struct jsonObj *jsonObjFromBoxedBuffer(sds buffer, sds populate_as) {
    unsigned char box = buffer[0];

    /* Chop off first box byte from buffer */
    /* benchmark against:
       char killbox[2] = {0};
       killbox[0] = box;
       sdstrim(buffer, killbox); */
    sdsrange(buffer, 1, -1);
    D("Decoding from buffer with box [%s] and key [%s]\n", bb(box),
      populate_as);
    return jsonObjFromBox(box, buffer, populate_as);
}

/* ====================================================================
 * Redis Data Extraction Helpers
 * ==================================================================== */
static long jsonListLength(robj *key) {
    robj *o;
    if (!(o = lookupKeyRead(g.c->db, key)))
        return -1;

    return listTypeLength(o);
}

/* Largely copied from lrangeCommand() */
static struct jsonObj *jsonObjFromRedisListByPosition(robj *key, int decode_as,
                                                      int position,
                                                      bool return_all) {
    struct jsonObj *member = NULL;
    redisClient *c = g.c_noreturn;
    robj *o;
    int start = 0; /* start at beginning */
    int i = 0;     /* current positon in the list */
    sds populate_as;

    if (!(o = lookupKeyRead(c->db, key)))
        return jsonObjCreateList(); /* return empty list if key not found */

    long llen = listTypeLength(o);

    /* don't allow any negative offsets because that breaks our
     * key guarantee that keys + positions can be joined and concatenated
     * to form top-level access keys. */
    if ((position > llen) || (!return_all && position < 0))
        return NULL;

    struct jsonObj *f = jsonObjCreateList();
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p = ziplistIndex(o->ptr, start);
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;
        sds buffer;

        while (llen--) {
            ziplistGet(p, &vstr, &vlen, &vlong);
            if (!return_all) {
                /* !return_all == only return one item */
                if (i < position) {
                    /* If we want a specific element, skip over elements
                     * that aren't at our target index. */
                    p = ziplistNext(o->ptr, p);
                    i++;
                    continue;
                } else if (i > position) {
                    /* The previous loop gave us our element.  Return it. */
                    return f;
                }
            }
            if (vstr) {
                D("[%s]: List Ziplist decoding str [%s], as (%d)\n", key->ptr,
                  vstr, decode_as);
                switch (decode_as) {
                case DECODE_ALL_NUMBER:
                    member = jsonObjNumberAsStringLen((char *)vstr, vlen);
                    break;
                case DECODE_ALL_STRING:
                    member = jsonObjStringLen((char *)vstr, vlen);
                    break;
                case DECODE_INDIVIDUAL:
                    populate_as = sdsAppendColonInteger(key->ptr, i);
                    buffer = sdsnewlen(vstr, vlen);
                    member = jsonObjFromBoxedBuffer(buffer, populate_as);
                    sdsfree(populate_as);
                    break;
                }
            } else {
                D("[%s]: List Ziplist decoding number [%lld], as (%d)\n",
                  key->ptr, vlong, decode_as);
                switch (decode_as) {
                case DECODE_ALL_NUMBER:
                    member = jsonObjNumberLongLong(vlong);
                    break;
                case DECODE_ALL_STRING:
                    member = jsonObjStringTake(sdsfromlonglong(vlong));
                    break;
                case DECODE_INDIVIDUAL:
                    D("ERROR - Trying to decode NUMBER as individual member.  "
                      "Not supported because we can't unpack a box from a "
                      "number.");
                    break;
                }
            }
            if (!member)
                member = jsonObjCreateList();
            jsonObjAddField(f, member);
            p = ziplistNext(o->ptr, p);
            i++;
        }
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        listNode *ln;

        ln = listIndex(o->ptr, start);

        while (llen--) {
            D("[%s]: List Linked decoding [%s]\n", key->ptr, ln->value);
            if (!return_all) {
                /* !return_all == only return one item */
                if (i < position) {
                    /* If we want a specific element, skip over elements
                     * that aren't at our target index. */
                    ln = ln->next;
                    i++;
                    continue;
                } else if (i > position) {
                    /* The previous loop gave us our element.  Return it. */
                    return f;
                }
            }
            switch (decode_as) {
            case DECODE_ALL_NUMBER:
                member = jsonObjNumberAsString(ln->value);
                break;
            case DECODE_ALL_STRING:
                member = jsonObjString(ln->value);
                break;
            case DECODE_INDIVIDUAL:
                /* parse as box then create sub-type */
                populate_as = sdsAppendColonInteger(key->ptr, i);
                member = jsonObjFromBoxedBuffer(sdsdup(ln->value), populate_as);
                sdsfree(populate_as);
                break;
            }
            if (!member)
                member = jsonObjCreateList();
            jsonObjAddField(f, member);
            ln = ln->next;
            i++;
        }
    }
    return f;
}

static struct jsonObj *jsonObjFromRedisList(robj *key, int decode_as) {
    return jsonObjFromRedisListByPosition(key, decode_as, -1,
                                          true); /* return all */
}

static struct jsonObj *jsonObjFromRedisListAtIndex(robj *key, int decode_as,
                                                   int idx) {
    struct jsonObj *f =
        jsonObjFromRedisListByPosition(key, decode_as, idx, false);

    if (!f)
        return NULL;

    D("GOT RESULTS: %d!\n", f->content.obj.elements);
    struct jsonObj *o = NULL;
    if (f->content.obj.elements == 1) {
        /* Extract the returned element to return */
        o = f->content.obj.fields[0];
        f->content.obj.fields[0] = NULL;
    }

    jsonObjFree(f);

    return o;
}

static struct jsonObj *jsonObjFromRedisListAtIndexSdsKey(sds key, int decode_as,
                                                         int idx) {
    /* The keys to this function are already boxed */
    robj *boxed_key = dbstr(key);
    struct jsonObj *o = jsonObjFromRedisListAtIndex(boxed_key, decode_as, idx);
    decrRefCount(boxed_key);
    return o;
}

/* Based on decode type, generate [BOX]key so we can return the correct list. */
static struct jsonObj *jsonObjFromRedisListSdsKey(sds key, int decode_as) {
    sds access_key = boxListByDecode(key, decode_as);
    robj *boxed_key = dbstrTake(access_key);
    struct jsonObj *f = jsonObjFromRedisList(boxed_key, decode_as);
    decrRefCount(boxed_key);
    return f;
}

static struct jsonObj *localrpop(sds key, int decode_as) {
    redisClient *c = g.c;

    /* Get length of list before we alter it */
    robj *lookup_key = dbstr(key);
    long len = jsonListLength(lookup_key);

    robj *argv[2] = { 0 };
    argv[1] = lookup_key;
    c->argc = 2;
    c->argv = argv;

    rpopCommand(c);

    decrRefCount(argv[1]);
    clearClient(c);

    sds fake_client_buffer = fakeClientResultBuffer(c);

    char *next;
    /* We only need to read format $[SZ]\r\n[DATA] from buffer */
    int sz = strtol(fake_client_buffer + 1, &next, 10);
    next += SZ_CRLF;

    sds pop_result = sdsnewlen(next, sz);
    sdsfree(fake_client_buffer);

    /* If we know this is a number or string, quickly return without
     * attempting recursive processing.  The missing case of DECODE_INDIVIDUAL
     * will happen if we don't return anything here. */
    switch (decode_as) {
    case DECODE_ALL_STRING:
        return jsonObjStringTake(pop_result);
        break;
    case DECODE_ALL_NUMBER:
        return jsonObjNumberAsStringTake(pop_result);
        break;
    }

    /* lists are 0-based, so the last element is (len-1) */
    sds populate_as = sdsAppendColonInteger(key, len - 1);
    struct jsonObj *f = jsonObjFromBoxedBuffer(pop_result, populate_as);

    /* If we're removing a container type, locate and delete it's top-level key.
     */
    /* Beacuse this is a list, the 'field' is just the position. */
    if (len > 0 && (f->type == JSON_TYPE_MAP || f->type == JSON_TYPE_LIST)) {
        sds positional = sdsfromlonglong(len - 1);
        findAndRecursivelyDelete(key, positional);
        sdsfree(positional);
    }
    sdsfree(populate_as);

    return f;
}

void rpopRecursiveAndReply(redisClient *c, sds key, int decode_as) {
    struct jsonObj *f = localrpop(key, decode_as);

    sds json = jsonObjToJson(f);
    jsonObjFree(f);

    addReplyBulkCBuffer(c, json, sdslen(json));

    sdsfree(json);
}
