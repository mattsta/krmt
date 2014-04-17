#ifndef __GEOJSON_H__
#define __GEOJSON_H__

#include "redis.h"
#include "geohash_helper.h"

struct geojsonPoint {
    double latitude;
    double longitude;
    double dist;
    char *set;
    char *member;
    void *userdata;
};

sds geojsonLatLongToPointFeature(const double latitude, const double longitude,
                                 const char *set, const char *member,
                                 const double dist, const char *units);
sds geojsonBoxToPolygonFeature(const double x1, const double y1,
                               const double x2, const double y2,
                               const char *set, const char *member);
sds geojsonFeatureCollection(const struct geojsonPoint *pts, const size_t len,
                             const char *units);

#endif
