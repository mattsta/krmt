#include "i_yajl.h"
#include "jsonobj.h"

/* ====================================================================
 * yajl callbacks
 * ==================================================================== */

/*#define GEN_AND_RETURN(func) \
    {                                                                          \
        yajl_gen_status __stat = func;                                         \
        if (__stat == yajl_gen_generation_complete && s_streamredis) {         \
            yajl_gen_reset(g, "\n");                                           \
            __stat = func;                                                     \
        }                                                                      \
        return __stat == yajl_gen_status_ok;                                   \
    }
*/
#define GEN_AND_RETURN(func) return func;
#define previous_container(_l) listNodeValue(listLast(_l))

/* ====================================================================
 * Global state for turning json into (jsonObj *)
 * ==================================================================== */
struct ctxstate {
    list *mapstack;
    struct jsonObj *current;
    sds keyname;
};

/* ====================================================================
 * yajl parsing callbacks
 * ==================================================================== */
#define assign_name(_obj)                                                      \
    do {                                                                       \
        D("Adding name to object...\n");                                       \
        (_obj)->name = state->keyname;                                         \
        state->keyname = NULL;                                                 \
    } while (0)

#define process_field(_obj)                                                    \
    do {                                                                       \
        if (!state->current) {                                                 \
            state->current = (_obj);                                           \
        } else if (!jsonObjAddField(state->current, (_obj))) {                 \
            jsonObjFree(_obj);                                                 \
            return false;                                                      \
        }                                                                      \
    } while (0)

static int redis_parse_null(void *ctx) {
    struct ctxstate *state = ctx;

    struct jsonObj *o = jsonObjCreate();
    o->type = JSON_TYPE_NULL;
    assign_name(o);
    process_field(o);
    return true;
}

static int redis_parse_boolean(void *ctx, int boolean) {
    struct ctxstate *state = ctx;

    struct jsonObj *o = jsonObjCreate();
    o->type = boolean ? JSON_TYPE_TRUE : JSON_TYPE_FALSE;
    assign_name(o);
    process_field(o);
    return true;
}

static int redis_parse_number(void *ctx, const char *s, size_t l) {
    struct ctxstate *state = ctx;

    struct jsonObj *o = jsonObjNumberAsStringLen((char *)s, l);
    assign_name(o);
    process_field(o);
    return true;
}

static int redis_parse_string(void *ctx, const unsigned char *stringVal,
                              size_t stringLen) {
    struct ctxstate *state = ctx;

    struct jsonObj *o = jsonObjStringLen((char *)stringVal, stringLen);
    assign_name(o);
    process_field(o);
    return true;
}

static int redis_parse_map_key(void *ctx, const unsigned char *stringVal,
                               size_t stringLen) {
    struct ctxstate *state = ctx;
    state->keyname = sdsnewlen(stringVal, stringLen);
    D("Parsed key name [%s]\n", state->keyname);
    return true;
}

static void addRecursiveContainer(struct ctxstate *state,
                                  struct jsonObj *newcontainer) {
    assign_name(newcontainer);

    /* Every container except the root is a member of a previous container */
    if (state->current) {
        jsonObjAddField(state->current, newcontainer);
        listAddNodeTail(state->mapstack, state->current);
    }

    D("Setting current container to %p...\n", newcontainer);
    state->current = newcontainer;
}

static void removeRecursiveContainer(struct ctxstate *state) {
    /* If this closes the root container, there is no previous for us to
     * restore. */
    if (listLength(state->mapstack)) {
        state->current = previous_container(state->mapstack);
        D("Restoring previous container %p...\n", state->current);

        /* The previous container is now the current container.  We're
         * tracking it in state->current, so we can remove it from
         * the container stack. */
        listDelNode(state->mapstack, listLast(state->mapstack));
    } else {
        D("Not restoring previous container because already at root.\n");
    }
}

static int redis_parse_start_map(void *ctx) {
    struct jsonObj *newmap = jsonObjCreateMap();

    D("Starting map...\n");
    addRecursiveContainer(ctx, newmap);
    return 1;
}

static int redis_parse_end_map(void *ctx) {
    D("Ending map...\n");
    removeRecursiveContainer(ctx);
    return 1;
}

static int redis_parse_start_array(void *ctx) {
    struct jsonObj *newlist = jsonObjCreateList();

    D("Starting array...\n");
    addRecursiveContainer(ctx, newlist);
    return 1;
}

static int redis_parse_end_array(void *ctx) {
    D("Ending array...\n");
    removeRecursiveContainer(ctx);
    return 1;
}

/* ====================================================================
 * yajl parsing callback collection
 * ==================================================================== */
static yajl_callbacks callbacks = {.yajl_null = redis_parse_null,
                                   .yajl_boolean = redis_parse_boolean,
                                   .yajl_integer = NULL,
                                   .yajl_double = NULL,
                                   .yajl_number = redis_parse_number,
                                   .yajl_string = redis_parse_string,
                                   .yajl_start_map = redis_parse_start_map,
                                   .yajl_map_key = redis_parse_map_key,
                                   .yajl_end_map = redis_parse_end_map,
                                   .yajl_start_array = redis_parse_start_array,
                                   .yajl_end_array = redis_parse_end_array};

/* ====================================================================
 * Redis generators
 * ==================================================================== */
static int redis_gen_sds(void *ctx, const sds string) {
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(
        yajl_gen_string(g, (const unsigned char *)string, sdslen(string)));
}

static int redis_gen_double(void *ctx, const double number) {
    char numbuf[128] = {0};
    int sz = snprintf(numbuf, 128, "%f", number);
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(yajl_gen_number(g, numbuf, sz));
}

static int redis_gen_double_from_str(void *ctx, sds number) {
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(yajl_gen_number(g, (const char *)number, sdslen(number)));
}

static int redis_gen_start_map(void *ctx) {
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(yajl_gen_map_open(g));
}

static int redis_gen_end_map(void *ctx) {
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(yajl_gen_map_close(g));
}

static int redis_gen_start_list(void *ctx) {
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(yajl_gen_array_open(g));
}

static int redis_gen_end_list(void *ctx) {
    yajl_gen g = (yajl_gen)ctx;
    GEN_AND_RETURN(yajl_gen_array_close(g));
}

/* ====================================================================
 * Redis jsonObj Traversal and Encoding
 * ==================================================================== */
static void encodeJsonObj(yajl_gen g, struct jsonObj *f) {
    switch (f->type) {
    case JSON_TYPE_NUMBER_AS_STRING:
        redis_gen_double_from_str(g, f->content.string);
        break;
    case JSON_TYPE_NUMBER:
        redis_gen_double(g, f->content.number);
        break;
    case JSON_TYPE_TRUE:
        yajl_gen_bool(g, true);
        break;
    case JSON_TYPE_FALSE:
        yajl_gen_bool(g, false);
        break;
    case JSON_TYPE_NULL:
        yajl_gen_null(g);
        break;
    case JSON_TYPE_STRING:
        redis_gen_sds(g, f->content.string);
        break;
    case JSON_TYPE_LIST:
        redis_gen_start_list(g);
        for (int i = 0; i < f->content.obj.elements; i++) {
            encodeJsonObj(g, f->content.obj.fields[i]);
        }
        redis_gen_end_list(g);
        break;
    case JSON_TYPE_MAP:
        redis_gen_start_map(g);
        for (int i = 0; i < f->content.obj.elements; i++) {
            /* Set Name */
            redis_gen_sds(g, f->content.obj.fields[i]->name);
            /* Set Value */
            encodeJsonObj(g, f->content.obj.fields[i]);
        }
        redis_gen_end_map(g);
        break;
    }
}

/* ====================================================================
 * Interface Helpers
 * ==================================================================== */
static void *yzmalloc(void *ctx, size_t sz) { return zmalloc(sz); }

static void yzfree(void *ctx, void *ptr) { zfree(ptr); }

static void *yzrealloc(void *ctx, void *ptr, size_t sz) {
    return zrealloc(ptr, sz);
}

yajl_alloc_funcs allocFuncs = {
    .malloc = yzmalloc, .free = yzfree, .realloc = yzrealloc, .ctx = NULL};

static yajl_handle setupParser(void *ctx) {
    yajl_handle hand = yajl_alloc(&callbacks, &allocFuncs, ctx);

    yajl_config(hand, yajl_allow_comments, 1);

    return hand;
}

static void cleanupParser(yajl_handle hand) { yajl_free(hand); }

static yajl_gen setupEncoder() {
    yajl_gen g = yajl_gen_alloc(&allocFuncs);
    /* yajl_gen_config(g, yajl_gen_beautify, 1); */
    return g;
}

static void cleanupEncoder(yajl_gen g) { yajl_gen_free(g); }

static sds encodedJson(yajl_gen g) {
    const unsigned char *json_encoded;
    size_t len;

    yajl_gen_get_buf(g, &json_encoded, &len);
    sds result = sdsnewlen(json_encoded, len);
    yajl_gen_clear(g);

    return result;
}

/* ====================================================================
 * Interface
 * ==================================================================== */
sds yajl_encode(struct jsonObj *f) {
    yajl_gen g = setupEncoder();

    encodeJsonObj(g, f);
    sds result = encodedJson(g);
    cleanupEncoder(g);

    return result;
}

struct jsonObj *yajl_decode(sds json, sds *error) {
    struct ctxstate *state = zmalloc(sizeof(*state));
    struct jsonObj *root;

    state->mapstack = listCreate();
    state->current = NULL;
    state->keyname = NULL;

    yajl_handle hand = setupParser(state);

    yajl_status status =
        yajl_parse(hand, (const unsigned char *)json, sdslen(json));
    yajl_complete_parse(hand);

    root = state->current;
    if (status != yajl_status_ok) {
        if (error) {
            unsigned char *err = yajl_get_error(
                hand, 1, (const unsigned char *)json, sdslen(json));
            D("YAJL ERROR: %s\n", err);
            *error = sdscat(*error, (const char *)err);
            yajl_free_error(hand, err);
        }
        /* Remove any parsed json objects because the parse failed.  Whatever
         * was created doesn't represent the entire input. */
        jsonObjFree(root);
        root = NULL;
    }

    cleanupParser(hand);

    listRelease(state->mapstack);
    zfree(state);

    return root;
}
