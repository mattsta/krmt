#include "jsonobj_set.h"

/* ====================================================================
 * Create nested Redis types from jsonObj map/array
 * ==================================================================== */
/* mostly copied from pushGenericCommand() in t_list.c */
static bool storeListInsideRedisFromJsonObjFields(struct jsonObj *o, sds id) {
    if (!o || o->type != JSON_TYPE_LIST)
        return false;

    redisClient *c = g.c_noreturn;

    sds actual_key = boxKeyIfHomogeneous(o, id);
    robj *list_key = dbstrTake(actual_key);

    robj *lobj = lookupKeyWrite(c->db, list_key);

    /* If the list already exists, we're going to delete it so we
     * have a fresh start for our blanket add.  If someone adds the same
     * document again, they should get the document they set without
     * any influence from past elements. */
    if (lobj) {
        dbDelete(c->db, lobj);
        lobj = NULL;
    }

    /* We are guaranteed lobj doesn't exist here because if we
     * found it above, we deleted it. */
    if (!lobj) {
        lobj = createZiplistObject();
        dbAdd(c->db, list_key, lobj);
    }
    decrRefCount(list_key);

    for (int j = 0; j < o->content.obj.elements; j++) {
        struct jsonObj *field = o->content.obj.fields[j];
        robj *ro = dbstr(field->content.string);
        ro = tryObjectEncoding(ro);
        listTypePush(lobj, ro, REDIS_TAIL);
        decrRefCount(ro);
    }
    return true;
}

/* Direct Mode assumes zero nesting of types.  The jsonObj is *only* top level
 * string field->value pairs. */
static void hmsetDirectFromJson(struct jsonObj *r, sds key) {
    redisClient *c = g.c_noreturn;

    int total_pairs = r->content.obj.elements;
    c->argc = total_pairs * 2 + 2; /* +2 = [cmd, hash key] */
    robj *argv[c->argc];

    sds total_key = boxKeyIfHomogeneous(r, key);
    argv[0] = NULL;
    argv[1] = dbstrTake(total_key);

    D("Setting direct %s with %d pairs\n", key, total_pairs);
    for (int i = 0; i < total_pairs; i++) {
        struct jsonObj *o = r->content.obj.fields[i];

        if (!o)
            D("BAD—OBJECT IS NULL AT %d\n", i);

        if (o->type == JSON_TYPE_PTR)
            D("[%s] (%s) -> [%s]\n", key, o->name, bb(*o->content.string));
        else
            D("[%s] (%s) -> (%s)\n", key, o->name, o->content.string);

        argv[(i * 2) + 2] = dbstr(o->name);

        switch (o->type) {
        case JSON_TYPE_PTR:
        case JSON_TYPE_STRING:
        case JSON_TYPE_NUMBER_AS_STRING:
            argv[(i * 2) + 3] = dbstr(o->content.string);
            break;
        default:
            D("SETTING NON-STRING (%d) IN HMSET at %d for name: %s!\n", o->type,
              i, o->name);
            break;
        }
    }
    c->argv = argv;
    hmsetCommand(c);
    for (int i = 1; i < c->argc; i++) {
        decrRefCount(argv[i]);
    }
    clearClient(c);
}

/* Shrink jsonObj fields to (maybe boxed) strings for simple types */
static bool compactRepresentation(struct jsonObj *o) {
    switch (o->type) {
    case JSON_TYPE_MAP:
    case JSON_TYPE_LIST:
        /* If we are a container type, process the container below. */
        break;
    default:
        /* Else, we have an immediate type. */
        jsonObjBoxBasicType(o);
        return true;
        break;
    }

    if (o->content.obj.homogeneous) {
        switch (o->content.obj.subtype) {
        case JSON_TYPE_STRING:
        case JSON_TYPE_NUMBER_AS_STRING:
            /* If our fields are homogeneous strings or numbers, we use no
             * boxing
             * because the box type is promoted to the front of the key for this
             * container, not for the individual values themselves. */
            return true;
            break;
        case JSON_TYPE_NUMBER:
        /* Not entirely sure if we ever encode NUMBER directly.  We always
         * seem to use NUMBER_AS_STRING.  Double check. */
        case JSON_TYPE_TRUE:
        case JSON_TYPE_FALSE:
        case JSON_TYPE_NULL:
            /* for True/False/Null homogeneous, we could avoid encoding them
             * directly and just append the number to return in the list
             * when we are reading.  That would break someone trying to
             * update the list itself though. */
            for (int i = 0; i < o->content.obj.elements; i++) {
                jsonObjBoxBasicType(o->content.obj.fields[i]);
            }
            return true;
            break;
        case JSON_TYPE_MAP:
        case JSON_TYPE_LIST:
            /* These cases would catch a container with only lists or only
             * maps.
             * It doesn't make sense to have a container type tagged as being
             * homogeneous with other container types, since you have to iterate
             * over the sub-containers anyway. */
            break;
        }
    }
    return false;
}

/* Generate the correct depth key for finding sub-containers of
 * higher level containers. */
static sds populateKey(sds base, struct jsonObj *root, struct jsonObj *field,
                       int i) {
    sds populate_as = NULL;
    sds position = NULL;

    /* If we have a name, we're making sub-types */
    if (field->name) {
        /* as we recurse more, this grows as: ID:subID1:subID2:...:subIDk */
        populate_as = sdsAppendColon(base, field->name);
    } else {
        /* If we are putting a map or list inside of a list, we need a special
         * positional designator.
         * Lists are guaranteed to not have names themselves. */
        /* If anybody inserts or delets from the list, we must rename all our
         * positional designators in the list. */
        switch (root->type) {
        case JSON_TYPE_LIST:
            switch (field->type) {
            case JSON_TYPE_MAP:
            case JSON_TYPE_LIST:
                position = sdsfromlonglong(i);
                populate_as = sdsAppendColon(base, position);
                sdsfree(position);
                break;
            }
        }
    }

    if (populate_as)
        return populate_as;
    else
        return sdsdup(base);
}

/* Take a jsonObj and create it in Redis as a tree of hashes/lists as
 * appropriate. */
static bool hmsetIndirectFromJson(struct jsonObj *root, sds id) {
    /* If the root type isn't a homogeneous container, process all sub-fields */
    if (!compactRepresentation(root)) {
        /* This object is either a map or a list. */
        int elements = root->content.obj.elements;

        D("Element count: %d\n", elements);
        /* For each field in this map or list... */
        for (int i = 0; i < elements; i++) {
            struct jsonObj *o = root->content.obj.fields[i];
            sds populate_as = populateKey(id, root, o, i);

            D("Root type, element type and subname: %d, %d, %s\n", root->type,
              o->type, populate_as);

            /* Actions: - if map or list, recursively create sub-containers
             *          - if string, number, true, false, null: box in place. */
            switch (o->type) {
            case JSON_TYPE_LIST:
                D("TYPE L\n");
            case JSON_TYPE_MAP:
                D("TYPE M\n");
                hmsetIndirectFromJson(o, populate_as);
                root->content.obj.fields[i] = jsonObjConvertToPtr(o);
                break;
            default:
                D("type D\n");
                jsonObjBoxBasicType(o);
                break;
            }
            sdsfree(populate_as);
        }
    }
#ifdef D
    /* D is too noisy and we need spacing between iterations.  Only do this
     * double line printf if we are in D-debug mode. */
    printf("\n\n");
#endif
    /* Now, all the way down here, every field inside the root jsonObj hierarchy
     * has been created as proper types, and their entires have been replaced
     * with boxed type pointers all the way up the hierarchy. Now we are
     * guaranteed a few things: the json is now one level of fields with boxed
     * pointers - OR - this is a homogeneous container with only strings
     * from the beginning. */

    switch (root->type) {
    case JSON_TYPE_MAP:
        D("Top level ID for Map: %s\n", id);
        hmsetDirectFromJson(root, id);
        break;
    case JSON_TYPE_LIST:
        D("Top level ID for List: %s\n", id);
        storeListInsideRedisFromJsonObjFields(root, id);
        break;
    }
    return true;
}

/* Public interface for serializing a jsonObj to Redis */
void jsonObjToRedisDB(robj *id, struct jsonObj *r) {
    hmsetIndirectFromJson(r, id->ptr);
}
