#include "jsonobj.h"

/* ====================================================================
 * jsonObj Constructors
 * ==================================================================== */
struct jsonObj *jsonObjCreate() {
    struct jsonObj *root = zmalloc(sizeof(*root));
    root->name = NULL;
    root->type = JSON_TYPE_NOTSET;

    return root;
}

/* How many fields we initially allocate for maps/lists */
#define INITIAL_FIELD_COUNT 32

struct jsonObj *jsonObjCreateMap() {
    struct jsonObj *root = jsonObjCreate();

    root->type = JSON_TYPE_MAP;
    root->content.obj.fields_sz = INITIAL_FIELD_COUNT;
    root->content.obj.homogeneous = false; /* Irrelevant with zero contents */
    root->content.obj.subtype = JSON_TYPE_NOTSET;
    root->content.obj.elements = 0;
    root->content.obj.fields = zcalloc(sizeof(*root->content.obj.fields) *
                                       root->content.obj.fields_sz);

    return root;
}

struct jsonObj *jsonObjCreateList() {
    struct jsonObj *o = jsonObjCreateMap();
    o->type = JSON_TYPE_LIST;
    return o;
}

struct jsonObj *jsonObjNumberAsStringTake(sds number) {
    struct jsonObj *o = jsonObjCreate();

    o->type = JSON_TYPE_NUMBER_AS_STRING;
    o->content.string = number;

    return o;
}

struct jsonObj *jsonObjNumberAsString(sds number) {
    return jsonObjNumberAsStringTake(sdsdup(number));
}

struct jsonObj *jsonObjNumberLongLong(long long number) {
    return jsonObjNumberAsStringTake(sdsfromlonglong(number));
}

struct jsonObj *jsonObjNumber(double number) {
    /* If we can represent this double as an integer, do that instead. */
    if (isfinite(number) && (long long)number == number) {
        return jsonObjNumberAsStringTake(sdsfromlonglong(number));
    } else {
        /* else, create a normal double number object. */
        struct jsonObj *o = jsonObjCreate();

        o->type = JSON_TYPE_NUMBER;
        o->content.number = number;

        return o;
    }
}

struct jsonObj *jsonObjNumberAsStringLen(char *str, size_t len) {
    return jsonObjNumberAsStringTake(sdsnewlen(str, len));
}

struct jsonObj *jsonObjStringTake(sds string) {
    struct jsonObj *o = jsonObjCreate();

    o->type = JSON_TYPE_STRING;
    o->content.string = string;

    return o;
}

struct jsonObj *jsonObjString(sds string) {
    return jsonObjStringTake(sdsdup(string));
}

struct jsonObj *jsonObjStringLen(char *string, size_t len) {
    return jsonObjStringTake(sdsnewlen(string, len));
}

/* ====================================================================
 * jsonObj Updaters
 * ==================================================================== */
void jsonObjTakeName(struct jsonObj *f, sds new_name) {
    if (f->name)
        sdsfree(f->name);

    f->name = new_name;
}

void jsonObjUpdateName(struct jsonObj *f, sds new_name) {
    jsonObjTakeName(f, sdsdup(new_name));
}

bool jsonObjAddField(struct jsonObj *o, struct jsonObj *field) {
    if (!o)
        return false;

    /* Only add fields to MAP and LIST types.  Anything else = error */
    switch (o->type) {
    case JSON_TYPE_MAP:
    case JSON_TYPE_LIST:
        break;
    default:
        return false;
    }

    char subtype = o->content.obj.subtype;
    if (subtype == JSON_TYPE_NOTSET) {
        /* This is the first field in our object.  Claim the type. */
        o->content.obj.homogeneous = true;
        o->content.obj.subtype = field->type;
    } else if (o->content.obj.homogeneous && (subtype != field->type)) {
        /* We're a new field with a different type than all previous fields.
         * We've broken the homogeneous condition. */
        o->content.obj.homogeneous = false;
    }

    /* If we are at max size, double the pointer allocation. */
    if (o->content.obj.elements == o->content.obj.fields_sz) {
        o->content.obj.fields_sz *= 2;
        o->content.obj.fields =
            zrealloc(o->content.obj.fields,
                     sizeof(*o->content.obj.fields) * o->content.obj.fields_sz);
    }

    o->content.obj.fields[o->content.obj.elements++] = field;
    return true;
}

/* ====================================================================
 * jsonObj Destructor (recursive)
 * ==================================================================== */
void jsonObjFree(struct jsonObj *f) {
    if (!f)
        return;

    switch (f->type) {
    case JSON_TYPE_NUMBER:
        break;
    case JSON_TYPE_PTR:
    case JSON_TYPE_STRING:
    case JSON_TYPE_NUMBER_AS_STRING:
        sdsfree(f->content.string);
        break;
    case JSON_TYPE_LIST:
    case JSON_TYPE_MAP:
        for (int i = 0; i < f->content.obj.elements; i++) {
            jsonObjFree(f->content.obj.fields[i]);
        }
        zfree(f->content.obj.fields);
        break;
    }

    sdsfree(f->name);
    zfree(f);
}
