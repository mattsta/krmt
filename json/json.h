#ifndef __JSON_H__
#define __JSON_H__

#include "redis.h"
#include <stdbool.h>

#ifdef PRODUCTION
#define D(...)
#else
#define D(...)                                                                 \
    do {                                                                       \
        printf("%s:%s:%d:\t", __FILE__, __FUNCTION__, __LINE__);               \
        printf(__VA_ARGS__);                                                   \
    } while (0)
#endif

/* When reading a container type (map/list), we can shortcircut decoding if
 * we know the types up front: */
#define DECODE_UNAVAILABLE 0
#define DECODE_ALL_NUMBER 128
#define DECODE_ALL_STRING 129
#define DECODE_INDIVIDUAL 130

/* strlen("\r\n") */
#define SZ_CRLF 2

/* Inbound type detectors for avoiding unnecessary recursive parsing */
#define ONLY_STRINGS(_obj)                                                     \
    ((_obj)->content.obj.homogeneous &&((_obj)->content.obj.subtype ==         \
                                        JSON_TYPE_STRING))

#define ONLY_NUMBERS(_obj)                                                     \
    ((_obj)->content.obj.homogeneous &&((_obj)->content.obj.subtype ==         \
                                        JSON_TYPE_NUMBER_AS_STRING))

/* Global things for json module */
struct global {
    redisClient *c; /* fake client acting like real client with output buffer */
    redisClient *c_noreturn; /* fake client without output buffer */
    robj *err_parse;
};

/* Created and managed by json.c */
extern struct global g;

robj *dbstrTake(sds id);
robj *dbstr(sds id);
void freeFakeClientResultBuffer(redisClient *c, sds reply);
sds fakeClientResultBuffer(redisClient *c);
void clearClient(redisClient *c);

/* ====================================================================
 * Binary Debugging
 * ==================================================================== */
/* Converts byte to an ASCII string of ones and zeroes */
/* 'bb' is easy to type and stands for "byte (to) binary (string)" */
static const char *bb(unsigned char x) {
    static char b[9] = { 0 };

    b[0] = x & 0x80 ? '1' : '0';
    b[1] = x & 0x40 ? '1' : '0';
    b[2] = x & 0x20 ? '1' : '0';
    b[3] = x & 0x10 ? '1' : '0';
    b[4] = x & 0x08 ? '1' : '0';
    b[5] = x & 0x04 ? '1' : '0';
    b[6] = x & 0x02 ? '1' : '0';
    b[7] = x & 0x01 ? '1' : '0';

    return b;
}

#endif
