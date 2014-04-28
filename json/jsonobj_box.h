#ifndef __JSONOBJ_BOX_H__
#define __JSONOBJ_BOX_H__

#include "json.h"
#include "jsonobj.h"

/* ====================================================================
 * Type Bits for Boxing JSON types in Redis Storage
 * ==================================================================== */
#define JBIT(bit) (1 << bit)

#define JHOMOGENEOUS JBIT(0)
#define JNUMBER JBIT(1)
#define JSTRING JBIT(2)
#define JMAP JBIT(3)
#define JLIST JBIT(4)
#define JTRUE JBIT(5)
#define JFALSE JBIT(6)
#define JNULL JBIT(7)
/* DO NOT define more unary J types.  The type box is a char. We
 * only have 8 bits (0-7) to work with. */

/* Valid boxes are:
 *   - JTRUE
 *   - JFALSE
 *   - JNULL
 *   - JSTRING
 *   - JNUMBER
 *   - JMAP
 *   - JLIST
 *   - JMAP|JHOMOGENEOUS|JNUMBER
 *   - JMAP|JHOMOGENEOUS|JSTRING
 *   - JLIST|JHOMOGENEOUS|JSTRING
 *   - JLIST|JHOMOGENEOUS|JNUMBER
 *   (We don't care about homogeneous container types containing only
 *    true, false, null, maps, or lists.)
 */

/* If we need more types, we can use compound types based on individual
 * types impossible to co-exist naturally (e.g. NULL|FALSE, MAP|LIST, etc) */

/* Note: The combination 01111011 must never be used because that's
 * an ASCII '{' and would break cluster slot detection.  It should be
 * easy to never create it because those bits represent the absurd combination
 * (homogeneous | number | map | list | true | false) */

/* Actions we need to take when we read [BOX] back from Redis */
#define BOX_FETCH_MAP_DECODE 64
#define BOX_FETCH_LIST_DECODE 65
#define BOX_PARSE_NUMBER_AFTER_BOX 66
#define BOX_PARSE_STRING_AFTER_BOX 67
#define BOX_DECODE_IMMEDIATE 68
#define BOX_FETCH_MAP_NUMBER 69
#define BOX_FETCH_MAP_STRING 70
#define BOX_FETCH_LIST_NUMBER 71
#define BOX_FETCH_LIST_STRING 72

sds sdsAppendColon(sds orig, sds append);
sds sdsAppendColonInteger(sds orig, int append);
sds boxListByDecode(sds key, int decode_as);
sds boxHashByDecode(sds key, int decode_as);
int openBoxAction(unsigned char box);
int findKeyForHash(sds key, sds *foundKey, int *type);
int findKeyForList(sds key, sds *foundKey, int *type);
int jsonObjBasicTypeFromBox(unsigned char box);
sds boxKeyIfHomogeneous(struct jsonObj *f, sds key);
bool jsonObjBoxBasicType(struct jsonObj *o);
struct jsonObj *jsonObjConvertToPtr(struct jsonObj *o);
sds boxHashByDecode(sds key, int decode_as);
sds boxListByDecode(sds key, int decode_as);
sds boxgen(unsigned char box, sds key);
bool isValidBoxKeyPrefix(unsigned char box);

#endif
