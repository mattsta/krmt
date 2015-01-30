/*
 * Copyright (c) 2013-2014, yinqiwen <yinqiwen@gmail.com>
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "geohash.h"

/**
 * Hashing works like this:
 * Divide the world into 4 buckets.  Label each one as such:
 *  -----------------
 *  |       |       |
 *  |       |       |
 *  | 0,1   | 1,1   |
 *  -----------------
 *  |       |       |
 *  |       |       |
 *  | 0,0   | 1,0   |
 *  -----------------
 */

bool geohashGetCoordRange(uint8_t coord_type, GeoHashRange *lat_range,
                          GeoHashRange *long_range) {
    switch (coord_type) {
    case GEO_WGS84_TYPE: {
        /* These are constraints from EPSG:900913 / EPSG:3785 / OSGEO:41001 */
        /* We can't geocode at the north/south pole. */
        lat_range->max = 85.05112878;
        lat_range->min = -85.05112878;
        long_range->max = 180.0;
        long_range->min = -180.0;
        break;
    }
    case GEO_MERCATOR_TYPE: {
        lat_range->max = 20037726.37;
        lat_range->min = -20037726.37;
        long_range->max = 20037726.37;
        long_range->min = -20037726.37;
        break;
    }
    default: { return false; }
    }
    return true;
}

/* Interleave lower bits of x and y, so the bits of x
 * are in the even positions and bits from y in the odd;
 * x and y must initially be less than 2**32 (65536).
 * From:  https://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN
 */
static inline uint64_t interleave64(uint32_t xlo, uint32_t ylo) {
    static const uint64_t B[] = {0x5555555555555555, 0x3333333333333333,
                                 0x0F0F0F0F0F0F0F0F, 0x00FF00FF00FF00FF,
                                 0x0000FFFF0000FFFF};
    static const unsigned int S[] = {1, 2, 4, 8, 16};

    uint64_t x = xlo;
    uint64_t y = ylo;

    x = (x | (x << S[4])) & B[4];
    y = (y | (y << S[4])) & B[4];

    x = (x | (x << S[3])) & B[3];
    y = (y | (y << S[3])) & B[3];

    x = (x | (x << S[2])) & B[2];
    y = (y | (y << S[2])) & B[2];

    x = (x | (x << S[1])) & B[1];
    y = (y | (y << S[1])) & B[1];

    x = (x | (x << S[0])) & B[0];
    y = (y | (y << S[0])) & B[0];

    return x | (y << 1);
}

/* reverse the interleave process
 * derived from http://stackoverflow.com/questions/4909263
 */
static inline uint64_t deinterleave64(uint64_t interleaved) {
    static const uint64_t B[] = {0x5555555555555555, 0x3333333333333333,
                                 0x0F0F0F0F0F0F0F0F, 0x00FF00FF00FF00FF,
                                 0x0000FFFF0000FFFF, 0x00000000FFFFFFFF};
    static const unsigned int S[] = {0, 1, 2, 4, 8, 16};

    uint64_t x = interleaved;
    uint64_t y = interleaved >> 1;

    x = (x | (x >> S[0])) & B[0];
    y = (y | (y >> S[0])) & B[0];

    x = (x | (x >> S[1])) & B[1];
    y = (y | (y >> S[1])) & B[1];

    x = (x | (x >> S[2])) & B[2];
    y = (y | (y >> S[2])) & B[2];

    x = (x | (x >> S[3])) & B[3];
    y = (y | (y >> S[3])) & B[3];

    x = (x | (x >> S[4])) & B[4];
    y = (y | (y >> S[4])) & B[4];

    x = (x | (x >> S[5])) & B[5];
    y = (y | (y >> S[5])) & B[5];

    return x | (y << 32);
}

bool geohashEncode(GeoHashRange lat_range, GeoHashRange long_range,
                   double latitude, double longitude, uint8_t step,
                   GeoHashBits *hash) {
    if (NULL == hash || step > 32 || step == 0 || RANGEISZERO(lat_range) ||
        RANGEISZERO(long_range)) {
        return false;
    }

    hash->bits = 0;
    hash->step = step;

    if (latitude < lat_range.min || latitude > lat_range.max ||
        longitude < long_range.min || longitude > long_range.max) {
        return false;
    }

    double lat_offset =
        (latitude - lat_range.min) / (lat_range.max - lat_range.min);
    double long_offset =
        (longitude - long_range.min) / (long_range.max - long_range.min);

    /* convert to fixed point based on the step size */
    lat_offset *= (1 << step);
    long_offset *= (1 << step);

    uint32_t lat_offset_int = (uint32_t)lat_offset;
    uint32_t long_offset_int = (uint32_t)long_offset;

    hash->bits = interleave64(lat_offset_int, long_offset_int);
    return true;
}

bool geohashEncodeType(uint8_t coord_type, double latitude, double longitude,
                       uint8_t step, GeoHashBits *hash) {
    GeoHashRange r[2] = {{0}};
    geohashGetCoordRange(coord_type, &r[0], &r[1]);
    return geohashEncode(r[0], r[1], latitude, longitude, step, hash);
}

bool geohashEncodeWGS84(double latitude, double longitude, uint8_t step,
                        GeoHashBits *hash) {
    return geohashEncodeType(GEO_WGS84_TYPE, latitude, longitude, step, hash);
}

bool geohashEncodeMercator(double latitude, double longitude, uint8_t step,
                           GeoHashBits *hash) {
    return geohashEncodeType(GEO_MERCATOR_TYPE, latitude, longitude, step,
                             hash);
}

bool geohashDecode(const GeoHashRange lat_range, const GeoHashRange long_range,
                   const GeoHashBits hash, GeoHashArea *area) {
    if (HASHISZERO(hash) || NULL == area || RANGEISZERO(lat_range) ||
        RANGEISZERO(long_range)) {
        return false;
    }

    area->hash = hash;
    uint8_t step = hash.step;
    uint64_t hash_sep = deinterleave64(hash.bits); /* hash = [LAT][LONG] */

    double lat_scale = lat_range.max - lat_range.min;
    double long_scale = long_range.max - long_range.min;

    uint32_t ilato = hash_sep;       /* get lat part of deinterleaved hash */
    uint32_t ilono = hash_sep >> 32; /* shift over to get long part of hash */

    /* divide by 2**step.
     * Then, for 0-1 coordinate, multiply times scale and add
       to the min to get the absolute coordinate. */
    area->latitude.min =
        lat_range.min + (ilato * 1.0 / (1ull << step)) * lat_scale;
    area->latitude.max =
        lat_range.min + ((ilato + 1) * 1.0 / (1ull << step)) * lat_scale;
    area->longitude.min =
        long_range.min + (ilono * 1.0 / (1ull << step)) * long_scale;
    area->longitude.max =
        long_range.min + ((ilono + 1) * 1.0 / (1ull << step)) * long_scale;

    return true;
}

bool geohashDecodeType(uint8_t coord_type, const GeoHashBits hash,
                       GeoHashArea *area) {
    GeoHashRange r[2] = {{0}};
    geohashGetCoordRange(coord_type, &r[0], &r[1]);
    return geohashDecode(r[0], r[1], hash, area);
}

bool geohashDecodeWGS84(const GeoHashBits hash, GeoHashArea *area) {
    return geohashDecodeType(GEO_WGS84_TYPE, hash, area);
}

bool geohashDecodeMercator(const GeoHashBits hash, GeoHashArea *area) {
    return geohashDecodeType(GEO_MERCATOR_TYPE, hash, area);
}

bool geohashDecodeAreaToLatLong(const GeoHashArea *area, double *latlong) {
    double y, x;

    if (!latlong)
        return false;

    y = (area->latitude.min + area->latitude.max) / 2;
    x = (area->longitude.min + area->longitude.max) / 2;

    latlong[0] = y;
    latlong[1] = x;
    return true;
}

bool geohashDecodeToLatLongType(uint8_t coord_type, const GeoHashBits hash,
                                double *latlong) {
    GeoHashArea area = {{0}};
    if (!latlong || !geohashDecodeType(coord_type, hash, &area))
        return false;
    return geohashDecodeAreaToLatLong(&area, latlong);
}

bool geohashDecodeToLatLongWGS84(const GeoHashBits hash, double *latlong) {
    return geohashDecodeToLatLongType(GEO_WGS84_TYPE, hash, latlong);
}

bool geohashDecodeToLatLongMercator(const GeoHashBits hash, double *latlong) {
    return geohashDecodeToLatLongType(GEO_MERCATOR_TYPE, hash, latlong);
}

static void geohash_move_x(GeoHashBits *hash, int8_t d) {
    if (d == 0)
        return;

    uint64_t x = hash->bits & 0xaaaaaaaaaaaaaaaaLL;
    uint64_t y = hash->bits & 0x5555555555555555LL;

    uint64_t zz = 0x5555555555555555LL >> (64 - hash->step * 2);

    if (d > 0) {
        x = x + (zz + 1);
    } else {
        x = x | zz;
        x = x - (zz + 1);
    }

    x &= (0xaaaaaaaaaaaaaaaaLL >> (64 - hash->step * 2));
    hash->bits = (x | y);
}

static void geohash_move_y(GeoHashBits *hash, int8_t d) {
    if (d == 0)
        return;

    uint64_t x = hash->bits & 0xaaaaaaaaaaaaaaaaLL;
    uint64_t y = hash->bits & 0x5555555555555555LL;

    uint64_t zz = 0xaaaaaaaaaaaaaaaaLL >> (64 - hash->step * 2);
    if (d > 0) {
        y = y + (zz + 1);
    } else {
        y = y | zz;
        y = y - (zz + 1);
    }
    y &= (0x5555555555555555LL >> (64 - hash->step * 2));
    hash->bits = (x | y);
}

void geohashNeighbors(const GeoHashBits *hash, GeoHashNeighbors *neighbors) {
    neighbors->east = *hash;
    neighbors->west = *hash;
    neighbors->north = *hash;
    neighbors->south = *hash;
    neighbors->south_east = *hash;
    neighbors->south_west = *hash;
    neighbors->north_east = *hash;
    neighbors->north_west = *hash;

    geohash_move_x(&neighbors->east, 1);
    geohash_move_y(&neighbors->east, 0);

    geohash_move_x(&neighbors->west, -1);
    geohash_move_y(&neighbors->west, 0);

    geohash_move_x(&neighbors->south, 0);
    geohash_move_y(&neighbors->south, -1);

    geohash_move_x(&neighbors->north, 0);
    geohash_move_y(&neighbors->north, 1);

    geohash_move_x(&neighbors->north_west, -1);
    geohash_move_y(&neighbors->north_west, 1);

    geohash_move_x(&neighbors->north_east, 1);
    geohash_move_y(&neighbors->north_east, 1);

    geohash_move_x(&neighbors->south_east, 1);
    geohash_move_y(&neighbors->south_east, -1);

    geohash_move_x(&neighbors->south_west, -1);
    geohash_move_y(&neighbors->south_west, -1);
}
