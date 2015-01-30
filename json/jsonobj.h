#ifndef __JSONOBJ_H__
#define __JSONOBJ_H__

#include "json.h"
#include "solarisfixes.h"

#include <math.h>
/* ====================================================================
 * The Cannonical jsonObj
 * ==================================================================== */
struct jsonObj {
    sds name;
    union {
        double number;
        sds string;
        struct {
            bool homogeneous; /* true if all fields are the same type */
            int fields_sz;    /* Total number of allocated fields */
            int elements;     /* Number of used fields */
            struct jsonObj **fields;
            char subtype; /* If homogeneous, then subtype is type of fields */
        } obj;
    } content;
    char type;
};

/* ====================================================================
 * Types for jsonObj
 * ==================================================================== */
#define JSON_TYPE_NOTSET 0 /* we haven't assigned a type yet */
#define JSON_TYPE_NUMBER 1 /* double */
#define JSON_TYPE_STRING 2 /* raw data  -> redis hash value */
#define JSON_TYPE_MAP 3    /* json map  -> redis hash */
#define JSON_TYPE_LIST 4   /* json list -> redis list */
#define JSON_TYPE_TRUE 5
#define JSON_TYPE_FALSE 6
#define JSON_TYPE_NULL 7
#define JSON_TYPE_NUMBER_AS_STRING 8
#define JSON_TYPE_PTR 9

/* ====================================================================
 * jsonObj Manipulators
 * ==================================================================== */
struct jsonObj *jsonObjCreate();
struct jsonObj *jsonObjCreateMap();
struct jsonObj *jsonObjCreateList();

bool jsonObjAddField(struct jsonObj *o, struct jsonObj *field);

struct jsonObj *jsonObjNumber(double number);
struct jsonObj *jsonObjNumberLongLong(long long number);
struct jsonObj *jsonObjNumberAsStringTake(sds number);
struct jsonObj *jsonObjNumberAsString(sds number);
struct jsonObj *jsonObjNumberAsStringLen(char *str, size_t len);
struct jsonObj *jsonObjStringTake(sds string);
struct jsonObj *jsonObjString(sds string);
struct jsonObj *jsonObjStringLen(char *string, size_t len);

void jsonObjTakeName(struct jsonObj *f, sds new_name);
void jsonObjUpdateName(struct jsonObj *f, sds new_name);

void jsonObjFree(struct jsonObj *f);

#endif
