#ifndef __JSONOBJ_DELETE_H__
#define __JSONOBJ_DELETE_H__

#include "json.h"
#include "jsonobj.h"
#include "jsonobj_box.h"

int hgetallAndRecursivelyDeleteEverything(sds key);
int findAndRecursivelyDelete(sds key, sds field);

#endif
