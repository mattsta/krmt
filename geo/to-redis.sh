#!/bin/bash

here="`dirname $0`"
redis_dir="$here/../../redis"
redis_src_dir="$redis_dir/src"
redis_makefile="$redis_src_dir/Makefile"
geohash_dir="$redis_dir/deps/geohash-int"

# Copy geohash library to deps
echo "Copying geohash library to deps..."
cp "$here"/geohash* "$geohash_dir"

if [[ ! -f "$geohash_dir/Makefile" ]]; then
    echo "You need to create "$geohash_dir/Makefile" manually, add it to redis/deps/Makefile, then add it to DEPENDENCY_TARGETS in $redis_makefile, then add the objects to the redis-server target, then add the geohash Makefile to deps/Makefile distclean target too."
fi

# Copy geo commands
echo "Copying geo commands to redis/src..."
cp "$here"/geo.{c,h} "$redis_src_dir"

# Modify Redis build parameters
grep geo.o "$redis_makefile" > /dev/null
if [[ $? -eq 1 ]]; then
    # geo isn't in the Makefile.  Let's add it.
    echo "Adding geo.o to Makefile"
    gsed -i '/^REDIS_SERVER_OBJ/ s/$/ geo.o/' "$redis_makefile"
fi

# zset.c traverses zsets without generate a client output buffer.
# This is just a placeholder until we have a better zset API that
# can return elements as a list (or the zset functions could take
# a generic function pointer argument to call each time a matching
# result is found).
echo "Copying custom zset functions to redis/src..."
cp "$here"/zset.{c,h} "$redis_src_dir"

grep " zset.o" "$redis_makefile" > /dev/null
if [[ $? -eq 1 ]]; then
    # geo isn't in the Makefile.  Let's add it.
    echo "Adding zset.o to Makefile"
    gsed -i '/^REDIS_SERVER_OBJ/ s/$/ zset.o/' "$redis_makefile"
fi

# Update primary Redis Command Table
grep georadiusbymember "$redis_src_dir/redis.c" > /dev/null
if [[ $? -eq 1 ]]; then
    echo "Please manually add the commands from module.c to $redis_src_dir/redis.c then include these prototypes in redis.h:"
    cat $here/geo.h
fi

grep -- '-I../deps/geohash-int' "$redis_makefile" > /dev/null
if [[ $? -eq 1 ]]; then
    echo "Adding ../deps/geohash-int to global include path..."
    gsed -i '/^FINAL_CFLAGS\+=/ s#$# -I../deps/geohash-int#' "$redis_makefile"
fi

echo "Copying geojson helper to redis/src..."
cp "$here"/geojson.{c,h} "$redis_src_dir"

# Modify Redis build parameters
grep geojson.o "$redis_makefile" > /dev/null
if [[ $? -eq 1 ]]; then
    # geo isn't in the Makefile.  Let's add it.
    echo "Adding geojson.o to Makefile..."
    gsed -i '/^REDIS_SERVER_OBJ/ s/$/ geojson.o/' "$redis_makefile"
fi

grep "static int zslValueLteMax" "$redis_src_dir/t_zset.c"
if [[ $? -eq 0 ]]; then
    # we need this to be not-static
    echo "Removing static from zslValueLteMax..."
    gsed -i 's/static int zslValueLteMax/int zslValueLteMax/' "$redis_src_dir/t_zset.c"
fi

echo "Copying geo tests..."
cp "$here"/geo.tcl "$redis_dir/tests/unit"
