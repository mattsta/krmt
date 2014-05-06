#include "jsonobj_delete.h"

static struct jsonObj *jsonObjDeleteFromBox(unsigned char box, sds populate_as);

/* ====================================================================
 * Recusively Delete Entire Document
 * ==================================================================== */
/* Largely copied from lrangeCommand() */
static int jsonObjGetListFromBufferAndDeleteAll(robj *key, int decode_as) {
    redisClient *c = g.c_noreturn;
    robj *o;
    int start = 0; /* start at beginning */
    int i = 0;     /* current positon in the list */
    sds populate_as;

    if (!(o = lookupKeyRead(c->db, key)))
        return 0;

    switch (decode_as) {
    case DECODE_ALL_NUMBER:
    case DECODE_ALL_STRING:
        /* delete the list and GET OUT NOW */
        D("Deleting list [%s] (because it has no pointers inside of it).\n",
          key->ptr);
        return dbDelete(c->db, key);
        break;
    }

    long llen = listTypeLength(o);

    /* If we get this far, that means every element of this list is a
     * boxed type.  We must unbox each value and recursively delete if
     * it is a pointer to another sub-container. */
    if (o->encoding == REDIS_ENCODING_ZIPLIST) {
        unsigned char *p = ziplistIndex(o->ptr, start);
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        while (llen--) {
            ziplistGet(p, &vstr, &vlen, &vlong);
            if (vstr) {
                D("[%s]: List Ziplist decoding str [%s], as (%d)\n", key->ptr,
                  vstr, decode_as);
                /* generate access key for this position in the list */
                populate_as = sdsAppendColonInteger(key->ptr, i);
                jsonObjDeleteFromBox(vstr[0], populate_as);
                sdsfree(populate_as);
            } else {
                D("[%s]: List Ziplist decoding number [%lld], as (%d)\n",
                  key->ptr, vlong, decode_as);
                D("ERROR--Trying to decode NUMBER as individual number.  Not "
                  "supported since we can't unbox a number.\n");
            }
            p = ziplistNext(o->ptr, p);
            i++;
        }
    } else if (o->encoding == REDIS_ENCODING_LINKEDLIST) {
        listNode *ln;

        ln = listIndex(o->ptr, start);

        while (llen--) {
            D("[%s]: List Linked decoding [%s]\n", key->ptr, ln->value);
            /* generate access key for this position in the list */
            populate_as = sdsAppendColonInteger(key->ptr, i);
            jsonObjDeleteFromBox(((sds)ln->value)[0], populate_as);
            sdsfree(populate_as);
            ln = ln->next;
            i++;
        }
    }
    return dbDelete(g.c->db, key);
}

/* Given a depth key and a decode type, generate the top-level boxed Redis
 * key we use to access this list, then open the list and delete
 * everything inside. */
static int jsonObjDeleteFromRedisListSdsKey(sds key, int decode_as) {
    sds access_key = boxListByDecode(key, decode_as);
    robj *akobj = dbstrTake(access_key);
    int deleted =
        jsonObjGetListFromBufferAndDeleteAll(akobj, DECODE_INDIVIDUAL);
    decrRefCount(akobj);
    return deleted;
}

/* Based on box type, either recursively delete more containers or
 * return because the current value will be destroyed with this container. */
static struct jsonObj *jsonObjDeleteFromBox(unsigned char box,
                                            sds populate_as) {
    struct jsonObj *o = NULL;

    switch (openBoxAction(box)) {
    case BOX_FETCH_MAP_DECODE:
        hgetallAndRecursivelyDeleteEverything(populate_as);
        break;
    case BOX_FETCH_LIST_DECODE:
        jsonObjDeleteFromRedisListSdsKey(populate_as, DECODE_INDIVIDUAL);
        break;
    case BOX_FETCH_MAP_NUMBER:
    case BOX_FETCH_LIST_NUMBER:
    case BOX_FETCH_MAP_STRING:
    case BOX_FETCH_LIST_STRING:
        D("deleting container because of homogeneous box [%s]\n", populate_as);
        sds actual_key = boxgen(box, populate_as);
        robj *akobj = dbstrTake(actual_key);
        dbDelete(g.c->db, akobj);
        decrRefCount(akobj);
        /* can delete list directly */
        break;
    case BOX_PARSE_NUMBER_AFTER_BOX:
    case BOX_PARSE_STRING_AFTER_BOX:
    case BOX_DECODE_IMMEDIATE:
        /* These types aren't pointers, so they get deleted
         * when the container itself is deleted. */
        break;
    default:
        D("ERR: Unavailable boxaction: (Boxtype: [%s], boxaction: [%d]\n",
          bb(box), openBoxAction(box));
        break;
    }
    return o;
}

/* Parse Redis output protocol and extract key/value pairs we need to
 * recursively
 * delete this map container and all sub-containers too. */
static int jsonObjDeleteMapFromBuffer(char *buf, int buffer_elements, sds key,
                                      int decode_as) {
    sds name = NULL;
    char *next = buf;
    sds populate_as = NULL;

    /* If buffer_elements is odd, we ignore the last element. */
    /* (Otherise, we end up with a "name" allocated, but it never
     * joins gets tied to an object, so it's a memory leak. */
    if (buffer_elements % 2 != 0) {
        D("ERROR - Deleting from map with an odd number of elements?  Element "
          "count: %d\n",
          buffer_elements);
        buffer_elements--;
    }

    /* If we got here without a key, we can't delete the key, so give up. */
    if (!key)
        return 0;

    /* Only recursively parse output if we contain sub-containers.
     * Else, just delete this hash without any bufer processing. */
    if (decode_as == DECODE_INDIVIDUAL) {
        for (int i = 0; i < buffer_elements; i++) {
            if (buf[0] != '$')
                D("Non bulk type in multibulk!  Can't KV this.\n");
            else
                buf++; /* skip over '$' */

            int sz = strtol(buf, &next, 10);
            next += SZ_CRLF;

            if (i % 2 == 0) {
                name = sdsnewlen(next, sz);
            } else {
                populate_as = sdsAppendColon(key, name);
                jsonObjDeleteFromBox(next[0], populate_as);
                sdsfree(populate_as);
                sdsfree(name);
            }
            next += sz + SZ_CRLF;
            buf = next;
        }
    }

    robj *dbkey = dbstr(key);
    int deleted = dbDelete(g.c->db, dbkey);
    decrRefCount(dbkey);
    return deleted;
}

/* Discover and recurse on multibulk reply contents.
 * We assume all multibulk return values are alternating K-V pairs. */
static int redisKVProtocolRecusivelyDelete(char *reply, sds key,
                                           int decode_as) {
    char *p = reply;
    char *after = reply + 1;
    char *next = NULL;
    int sz = strtol(after, &next, 10);
    next += SZ_CRLF; /* jump over \r\n */

    /* The first three are immediate data after their [TYPEID] */
    /* The last two have [TYPEID][SIZE][CRLF] */
    switch (*p) {
    case '+': /* Status */
    case '-': /* Error */
    case ':': /* Integer */
    case '$': /* Bulk */
        /* these types can't contain nested containers, so ignore. */
        break;
    case '*': /* Multi Bulk */
        return jsonObjDeleteMapFromBuffer(next, sz, key, decode_as);
        break;
    }

    return 0;
}

/* Use HGETALL to get an entire hash, the iterate over the hash
 * Key/Value pairs to delete any sub-containers. */
static int hgetallRecursivelyDeleteJsonObj(sds key, int decode_as) {
    redisClient *fake_client = g.c;

    fake_client->argc = 2;
    robj *argv[2] = { 0 };

    D("Access key for read: [%s] (potential box: [%s])\n", key, bb(key[0]));
    robj *proper_key = dbstr(key);

    argv[1] = proper_key;
    fake_client->argv = argv;

    hgetallCommand(fake_client);

    clearClient(fake_client);

    /* Coalesce client buffer into a single string we can iterate over */
    sds fake_client_buffer = fakeClientResultBuffer(fake_client);

    /* Delete ALL THE THINGS */
    int deleted = redisKVProtocolRecusivelyDelete(fake_client_buffer,
                                                  proper_key->ptr, decode_as);
    freeFakeClientResultBuffer(fake_client, fake_client_buffer);
    decrRefCount(proper_key);

    return deleted;
}

/* Locate the storage hash for the given key by first trying the two options
 * for boxed hashes, then falling back to the key itself. */
int hgetallAndRecursivelyDeleteEverything(sds key) {
    /* We don't know if the top level has will be [BOX]KEY or KEY, so try
     * a few first and pick the best one. */
    sds found;
    int type;
    int decode_as = findKeyForHash(key, &found, &type);

    if (!found)
        return 0;

    D("Found hash: [%s] from original key: [%s], decode as: %d\n", found, key,
      decode_as);

    int deleted = hgetallRecursivelyDeleteJsonObj(found, decode_as);
    sdsfree(found);
    return deleted;
}

static int deleteHashField(sds key, sds field) {
    robj *keyobj = dbstr(key);
    robj *fieldobj = dbstr(field);
    D("Deleting hash field [%s] on key [%s]\n", field, key);
    robj *hash = lookupKey(g.c->db, keyobj);
    int deleted = hashTypeDelete(hash, fieldobj);
    /* If this was the last field in the hash, we have to delete
     * the hash key itself too. */
    if (!hashTypeLength(hash))
        dbDelete(g.c->db, keyobj);
    decrRefCount(fieldobj);
    decrRefCount(keyobj);
    return deleted;
}

/* Generic deletion for fields and not top-level document keys */
int findAndRecursivelyDelete(sds key, sds field) {
    sds found;
    int type;
    int deleted = 0;
    int decode_as = findKeyForHash(key, &found, &type);

    robj *keyobj;
    sds combined;
    sds found_orig = found;
    switch (decode_as) {
    case DECODE_ALL_NUMBER:
    case DECODE_ALL_STRING:
        /* we can hdel directly because no nested containers exist */
        deleted = deleteHashField(found, field);
        break;
    case DECODE_INDIVIDUAL:
        /* If key is a *mixed* CONTAINER type, we can try to construct the
         * top-level key of this field and delete it directly */
        /* If the top level key isn't found, then the field either does not
         * exist *or* is a non-container-type (string, number, t/f/n) in a
         * mixed container. */
        combined = sdsAppendColon(found, field);
        decode_as = findKeyForHash(combined, &found, &type);
        if (decode_as == DECODE_UNAVAILABLE) {
            /* Unavailable = hash not found; search for list */
            decode_as = findKeyForList(combined, &found, &type);
            if (decode_as == DECODE_UNAVAILABLE) {
                /* Unavailable == List not found, so we can delete the field
                 * directly
                 * of this mixed-content hash. */
                deleted = deleteHashField(found_orig, field);
            } else {
                /* We found a top-level list we can recursively delete */
                keyobj = dbstr(found);
                deleted =
                    jsonObjGetListFromBufferAndDeleteAll(keyobj, decode_as);
                if (deleted)
                    deleteHashField(found_orig, field);
                decrRefCount(keyobj);
            }
        } else {
            /* Else, key:field is a top level hash and we can eradicate it. */
            /* it's either a hash or a list.  for now, we're being lazy and just
             * trying to delete both types. */
            switch (type) {
            case JSON_TYPE_MAP:
                deleted = hgetallRecursivelyDeleteJsonObj(found, decode_as);
                break;
            case JSON_TYPE_LIST:
                keyobj = dbstr(found);
                deleted +=
                    jsonObjGetListFromBufferAndDeleteAll(keyobj, decode_as);
                decrRefCount(keyobj);
                break;
            }
            /* If we deleted the value for the field, also delete the field */
            if (deleted)
                deleteHashField(found_orig, field);
        }
        sdsfree(found);
        sdsfree(combined);
        break;
    case DECODE_UNAVAILABLE:
        /* NOTE: currently we don't support removing *elements* of lists, but
         * we do support removing entire lists, and as a side effect of some of
         * these cases, we support turning sub-maps into empty maps ('{}') and
         * sub lists into empty lists ('[]') */
        /* First, search for this entire key:field as a list, then delete
         * it if it exists. */
        combined = sdsAppendColon(key, field);
        decode_as = findKeyForList(combined, &found, &type);
        keyobj = dbstr(combined);
        switch (decode_as) {
        case DECODE_ALL_NUMBER:
        case DECODE_ALL_STRING:
            /* we can delete this top key directly */
            deleted = dbDelete(g.c->db, keyobj);
            break;
        case DECODE_INDIVIDUAL:
            /* we can recursively delete this entire list */
            deleted = jsonObjGetListFromBufferAndDeleteAll(keyobj, decode_as);
            break;
        case DECODE_UNAVAILABLE:
            /* abort.  combined key:field list doesn't exist. */
            break;
        }
        decrRefCount(keyobj);
        sdsfree(found);
        sdsfree(combined);
    }
    sdsfree(found_orig);
    return deleted;
}
