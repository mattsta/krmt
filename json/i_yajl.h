#ifndef __I_YAJL_H__
#define __I_YAJL_H__

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include "json.h"
#include "jsonobj.h"

sds yajl_encode(struct jsonObj *f);
struct jsonObj *yajl_decode(sds buffer, sds *error);

#endif
