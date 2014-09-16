#define _GNU_SOURCE /* provides getline() on Linux */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "geohash.h"

static long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1e6;
    ust += tv.tv_usec;
    return ust;
}

#define TOTAL 200000
int main(int argc, char *argv[]) {

    double latlong[TOTAL * 2] = { 0 };

    FILE *fp;
    size_t len = 4096;
    char *line = malloc(len);
    ssize_t read;
    bool use_file = false;

    if (argc == 2) {
        use_file = true;
        /* Warning: we attempt to read your file for exactly 200,000 lines */
        fp = fopen(argv[1], "r");
    }

    if (use_file) {
        printf("Loading 200,000 coordinate pairs from source file...\n");
        for (int i = 0; (read = getline(&line, &len, fp)) != -1 && i < TOTAL;
             i++) {
            char *err;

            latlong[i] = strtod(line, &err);
            latlong[i + 1] = strtod(err, &err);
        }
    } else {
        for (int i = 0; i < TOTAL; i++) {
            latlong[i] = 40.7126674;
            latlong[i + 1] = -74.0131594;
        }
    }

    printf("Sanity check: first latlong: (%f, %f)\n", latlong[0], latlong[1]);

    printf("Running an encode speed test...\n");

    long total = sizeof(latlong) / sizeof(*latlong) - 1;

/* encode speed test */
#define INNER_BOOST 2000
    GeoHashBits hash;
    long long start = ustime();
    for (long i = 0; i < total; i++)
        for (int j = 0; j < INNER_BOOST; j++)
            geohashEncodeWGS84(latlong[i], latlong[i + 1], GEO_STEP_MAX, &hash);
    long long end = ustime();

    double elapsed_seconds = (double)(end - start) / (double)1e6;
    printf("Elapsed encode time: %f seconds\n", elapsed_seconds);
    printf("Against %d total encodes\n", TOTAL * INNER_BOOST);
    printf("Speed: %f encodes per second\n",
           (TOTAL * INNER_BOOST) / elapsed_seconds);
    printf("(That's %f nanoseconds per encode)\n",
           elapsed_seconds * 1e9 / (TOTAL * INNER_BOOST));

    printf("\n");
    printf("Now running a decode speed test...\n");

    /* decode speed test */
    GeoHashArea area;
    start = ustime();
    for (long i = 0; i < total; i++)
        for (int j = 0; j < INNER_BOOST; j++)
            geohashDecodeWGS84(hash, &area);
    end = ustime();

    elapsed_seconds = (end - start) / 1e6;
    printf("Elapsed decode time: %f seconds\n", elapsed_seconds);
    printf("Against %d total decodes \n", TOTAL * INNER_BOOST);
    printf("Speed: %f decodes per second\n",
           (TOTAL * INNER_BOOST) / elapsed_seconds);
    printf("(That's %f nanoseconds per decode)\n",
           elapsed_seconds * 1e9 / (TOTAL * INNER_BOOST));

    exit(EXIT_SUCCESS);
}
