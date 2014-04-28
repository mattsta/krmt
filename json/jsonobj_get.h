#ifndef __JSONOBJ_GET_H__
#define __JSONOBJ_GET_H__

#include "json.h"
#include "jsonobj.h"
#include "jsonobj_box.h"
#include "jsonobj_delete.h"
#include "i_yajl.h"

void hgetallToJsonAndAndReply(redisClient *c, sds key);
void genericWrapCommandAndReply(redisClient *c);
void findJsonAndReply(redisClient *c, sds search_key);
void findJsonFieldAndReply(redisClient *c, sds container_key, sds field);

struct jsonObj *jsonObjFromBoxedBuffer(sds buffer, sds populate_as);
struct jsonObj *hgetToJsonObj(sds key, int decode_as, sds field);
void rpopRecursiveAndReply(redisClient *c, sds key, int decode_as);

#endif
