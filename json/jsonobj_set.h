#ifndef __JSONOBJ_SET_H__
#define __JSONOBJ_SET_H__

#include "json.h"
#include "jsonobj.h"
#include "jsonobj_box.h"

void jsonObjToRedisDB(robj *id, struct jsonObj *r);

#endif
