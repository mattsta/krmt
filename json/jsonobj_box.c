#include "jsonobj_box.h"

/* ====================================================================
 * jsonObj Box Management
 * ==================================================================== */
/* Given a box and a key, return a newly allocated [BOX]key */
sds boxgen(unsigned char box, sds key) {
    sds result = sdsnewlen(&box, 1);

    if (key)
        result = sdscatsds(result, key);

    return result;
}

static unsigned char jsonObjTagHomogeneous(struct jsonObj *t) {
    unsigned char boxtype = 0;

    /* Only tag homogeneous for container types */
    switch (t->type) {
    case JSON_TYPE_MAP:
        D("IS MAP\n");
        boxtype |= JMAP;
        break;
    case JSON_TYPE_LIST:
        D("IS LIST\n");
        boxtype |= JLIST;
        break;
    default:
        return 0;
        break;
    }

    /* Box based on internal homogeneous type */
    if (t->content.obj.homogeneous) {
        D("IS HOMOGENEOUS\n");
        boxtype |= JHOMOGENEOUS;
        switch (t->content.obj.subtype) {
        case JSON_TYPE_NUMBER:
        case JSON_TYPE_NUMBER_AS_STRING:
            D("IS NUMBER\n");
            boxtype |= JNUMBER;
            break;
        case JSON_TYPE_STRING:
            D("IS STRING\n");
            boxtype |= JSTRING;
            break;
        case JSON_TYPE_MAP:
        case JSON_TYPE_LIST:
        case JSON_TYPE_TRUE:
        case JSON_TYPE_FALSE:
        case JSON_TYPE_NULL:
            /* Containers with these homogeneous subtypes aren't special.
             * We still must decode each member individually instead of being
             * returning a bulk HGETALL */
            boxtype = 0;
            break;
        }
        D("Returning BOXTYPE: [%s] (subtype: %d)\n", bb(boxtype),
          t->content.obj.subtype);
        return boxtype;
    } else {
        return 0;
    }
}

/* Based on jsonObj type, return [BOX] for that type. */
static unsigned char jsonObjBoxType(struct jsonObj *t) {
    if (!t)
        return 0;

    /* The first byte of every type will be _boxtype_
     * UNLESS the entire type is homogeneous, then we
     * use direct values in the containers and _boxtype_ is
     * promoted to the first byte of the key. */
    unsigned char boxtype = 0;

    /* Define the box type byte */
    switch (t->type) {
    case JSON_TYPE_MAP:
        boxtype = JMAP | jsonObjTagHomogeneous(t);
        break;
    case JSON_TYPE_LIST:
        boxtype = JLIST | jsonObjTagHomogeneous(t);
        break;
    case JSON_TYPE_STRING:
        boxtype = JSTRING;
        break;
    case JSON_TYPE_NUMBER:
    case JSON_TYPE_NUMBER_AS_STRING:
        boxtype = JNUMBER;
        break;
    case JSON_TYPE_TRUE:
        boxtype = JTRUE;
        break;
    case JSON_TYPE_FALSE:
        boxtype = JFALSE;
        break;
    case JSON_TYPE_NULL:
        boxtype = JNULL;
        break;
    case JSON_TYPE_PTR:
        D("ERROR - TRIED TO BOX PTR FOR %s\n", t->name);
        break;
    }

    return boxtype;
}

/* Return a newly allocated [BOX] for the given object. */
static sds jsonObjGenerateBox(struct jsonObj *o) {
    if (!o)
        return NULL;

    char boxtype = jsonObjBoxType(o);

    return boxgen(boxtype, NULL);
}

struct jsonObj *jsonObjConvertToPtr(struct jsonObj *o) {
    struct jsonObj *ptrobj = jsonObjCreate();

    ptrobj->name = o->name;
    ptrobj->type = JSON_TYPE_PTR;
    ptrobj->content.string = jsonObjGenerateBox(o);

    D("Converting Obj Name %s to PTR [%s]\n", ptrobj->name,
      bb(*ptrobj->content.string));

    o->name = NULL; /* We took the original name, so don't free it. */
    jsonObjFree(o);

    return ptrobj;
}

static bool jsonObjBoxNumber(struct jsonObj *o) {
    unsigned char box = jsonObjBoxType(o);
    sds n;

    switch (o->type) {
    case JSON_TYPE_NUMBER:
        o->content.string =
            boxgen(box, sdscatprintf(sdsempty(), "%f", o->content.number));
        break;
    case JSON_TYPE_NUMBER_AS_STRING:
        n = o->content.string;
        o->content.string = boxgen(box, n);
        sdsfree(n);
        break;
    default:
        D("ERROR - Attempted to turn non-number type (%d) into number "
          "object.\n",
          o->type);
        return false;
    }

    o->type = JSON_TYPE_PTR;
    return true;
}

static bool jsonObjBoxString(struct jsonObj *o) {
    unsigned char box;
    sds orig_str = o->content.string;

    box = jsonObjBoxType(o);
    o->type = JSON_TYPE_PTR;

    o->content.string = boxgen(box, orig_str);
    sdsfree(orig_str);

    return true;
}

/* If type is not a container, box it. */
bool jsonObjBoxBasicType(struct jsonObj *o) {
    unsigned char box;

    switch (o->type) {
    case JSON_TYPE_MAP:
    case JSON_TYPE_LIST:
        return false;
        break;
    case JSON_TYPE_NUMBER:
    case JSON_TYPE_NUMBER_AS_STRING:
        jsonObjBoxNumber(o);
        break;
    case JSON_TYPE_STRING:
        jsonObjBoxString(o);
        break;
    case JSON_TYPE_TRUE:
    case JSON_TYPE_FALSE:
    case JSON_TYPE_NULL:
        box = jsonObjBoxType(o);
        o->type = JSON_TYPE_PTR;
        o->content.string = boxgen(box, NULL);
        break;
    default:
        D("ERROR - Tried to box invalid type (%d)\n", o->type);
        return false;
    }

    return true;
}

/* inverse of boxBasicType for immediate decode boxes */
int jsonObjBasicTypeFromBox(unsigned char box) {
    if (box & JTRUE)
        return JSON_TYPE_TRUE;
    else if (box & JFALSE)
        return JSON_TYPE_FALSE;
    else if (box & JNULL)
        return JSON_TYPE_NULL;
    else
        return -1;
}

/* When reading a [BOX] from storage, determine the
 * action needed to fully decode the type */
int openBoxAction(unsigned char box) {
    switch (box) {
    case JMAP:
        return BOX_FETCH_MAP_DECODE;
        break;
    case JLIST:
        return BOX_FETCH_LIST_DECODE;
        break;
    case JNUMBER:
        return BOX_PARSE_NUMBER_AFTER_BOX;
        break;
    case JSTRING:
        return BOX_PARSE_STRING_AFTER_BOX;
        break;
    case JTRUE:
    case JFALSE:
    case JNULL:
        return BOX_DECODE_IMMEDIATE;
        break;
    case JMAP | JHOMOGENEOUS | JSTRING:
        return BOX_FETCH_MAP_STRING;
        break;
    case JMAP | JHOMOGENEOUS | JNUMBER:
        return BOX_FETCH_MAP_NUMBER;
        break;
    case JLIST | JHOMOGENEOUS | JNUMBER:
        return BOX_FETCH_LIST_NUMBER;
        break;
    case JLIST | JHOMOGENEOUS | JSTRING:
        return BOX_FETCH_LIST_STRING;
        break;
    case JMAP | JHOMOGENEOUS | JNULL:
    case JMAP | JHOMOGENEOUS | JTRUE:
    case JMAP | JHOMOGENEOUS | JFALSE:
        return BOX_FETCH_MAP_DECODE;
        break;
    case JLIST | JHOMOGENEOUS | JNULL:
    case JLIST | JHOMOGENEOUS | JTRUE:
    case JLIST | JHOMOGENEOUS | JFALSE:
        /* All values in the list will be single boxes */
        /* Can be optimized to simpler fetches, but for now just
         * read all the element values. */
        return BOX_FETCH_LIST_DECODE;
        break;
    default:
        D("ERROR - Unknown box type: [%s, %d]\n", bb(box), box);
        return 0;
        break;
    }
}

/* If we are decoding an ALL* type, generate the proper boxed key so we
 * can read the container. */
static sds boxByDecode(unsigned char typebox, sds key, int decode_as) {
    unsigned char box = 0;

    /* Homogeneous storage containers have their keys prefixed
     * with their box, so we have to add the box to our depth
     * key to find the container. */
    switch (decode_as) {
    case DECODE_ALL_NUMBER:
        box = typebox | JHOMOGENEOUS | JNUMBER;
        break;
    case DECODE_ALL_STRING:
        box = typebox | JHOMOGENEOUS | JSTRING;
        break;
    }

    D("Box decode is returning with box [%s] (from [%s] with decode_as: %d)\n",
      bb(box), key, decode_as);
    if (box)
        return boxgen(box, key);
    else
        return sdsdup(key);
}

/* List key generation wrapper for boxByDecode */
sds boxListByDecode(sds key, int decode_as) {
    return boxByDecode(JLIST, key, decode_as);
}

/* Hash/map key generation wrapper for boxByDecode */
sds boxHashByDecode(sds key, int decode_as) {
    return boxByDecode(JMAP, key, decode_as);
}

/* We generate depth keys by concatenating the top level key
 * with each sub-key and join the results with a colon.
 * For non-key subtypes (i.e. positions in a list), we use
 * the position to denote the original element. */
sds sdsAppendColon(sds orig, sds append) {
    if (!orig)
        return NULL;

    if (!append)
        return sdsdup(orig);

    return sdscatsds(sdscatlen(sdsdup(orig), ":", 1), append);
}

sds sdsAppendColonInteger(sds orig, int append) {
    sds ll = sdsfromlonglong(append);
    sds appended = sdsAppendColon(orig, ll);
    sdsfree(ll);
    return appended;
}

/* Generate proper top-level storage keys for homogeneous types so we
 * (potentially) don't have to read every member of the container
 * when re-reading */
sds boxKeyIfHomogeneous(struct jsonObj *f, sds key) {
    unsigned char box = jsonObjTagHomogeneous(f);
    if (box) {
        D("Tagging [%s] as homogeneous\n", key);
        return boxgen(box, key);
    } else {
        return sdsdup(key);
    }
}

/* ====================================================================
 * Locate Redis Key by first trying Boxed versions
 * ==================================================================== */
/* The first top-level json hash key *may* be a boxed name to
 * denote if we can read all values without decoding further.
 * First try [MAP|ALLSTRING]key, then try [MAP|ALLNUMBER]key, then try key. */
static int findKeyForContainer(unsigned char container, sds key, sds *found_key,
                               int *type) {
    redisClient *c = g.c_noreturn;

    unsigned char only_strings_box = container | JHOMOGENEOUS | JSTRING;
    unsigned char only_numbers_box = container | JHOMOGENEOUS | JNUMBER;

    switch (container) {
    case JMAP:
        *type = JSON_TYPE_MAP;
        break;
    case JLIST:
        *type = JSON_TYPE_LIST;
        break;
    }

    /* Yes, this is a slight abuse of convention because we're using
     * dictFind() directly and not lookupKey() or dbExists(), but
     * dictFind lets us pass a sds directly which saves us a lot of
     * allocation/free copy/paste here. */
    sds stringbox = boxgen(only_strings_box, key);
    if (dictFind(c->db->dict, stringbox)) {
        D("Found All Strings Key\n");
        *found_key = stringbox;
        return DECODE_ALL_STRING;
    }
    sdsfree(stringbox);

    sds numbersbox = boxgen(only_numbers_box, key);
    if (dictFind(c->db->dict, numbersbox)) {
        D("Found All Numbers Key\n");
        *found_key = numbersbox;
        return DECODE_ALL_NUMBER;
    }
    sdsfree(numbersbox);

    dictEntry *b;
    if ((b = dictFind(c->db->dict, key))) {
        D("Found Mixed Container Key\n");
        *found_key = sdsdup(key);

        robj *bare = dictGetVal(b);
        if (bare->type == REDIS_HASH)
            *type = JSON_TYPE_MAP;
        else if (bare->type == REDIS_LIST)
            *type = JSON_TYPE_LIST;

        return DECODE_INDIVIDUAL;
    } else {
        D("Key not found for this container type.\n");
        *found_key = NULL;
        return DECODE_UNAVAILABLE;
    }
}

int findKeyForHash(sds key, sds *found_key, int *type) {
    D("Searching For Hash Using [%s]\n", key);
    return findKeyForContainer(JMAP, key, found_key, type);
}

int findKeyForList(sds key, sds *found_key, int *type) {
    D("Searching For List Using [%s]\n", key);
    return findKeyForContainer(JLIST, key, found_key, type);
}

bool isValidBoxKeyPrefix(unsigned char box) {
    switch (box) {
    case JMAP | JHOMOGENEOUS | JSTRING:
    case JMAP | JHOMOGENEOUS | JNUMBER:
    case JLIST | JHOMOGENEOUS | JNUMBER:
    case JLIST | JHOMOGENEOUS | JSTRING:
        return true;
        break;
    default:
        return false;
        break;
    }
}
