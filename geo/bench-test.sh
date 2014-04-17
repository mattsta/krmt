#!/bin/bash

# Purpose: Quickly get benchmark results for geo commands
# Secondary Purpose: To stress test Redis Geo commands by running
# them continuously under lldb, gdb, or valgrind.

PATH=~/repos/redis/src:$PATH
LOOPR=10000
SOCK=/tmp/redis.sock

ten() {
    CMD=$*

    echo $CMD
    (for i in {1..10}; do
        $CMD |grep "per second"
    done) |sort -n
    echo

}

# Compare Redis unix domain socket vs. Redis TCP socket speeds
# (Spoiler: Unix sockets are 10% to 35% faster than TCP sockets due to less overhead)
if [[ -f $SOCK ]]; then
ten redis-benchmark -s $SOCK -n $LOOPR geoencode 0 0
ten redis-benchmark -s $SOCK -n $LOOPR geoencode 0 0 withjson
ten redis-benchmark -s $SOCK -n $LOOPR geodecode 3471579339700058
ten redis-benchmark -s $SOCK -n $LOOPR geodecode 3471579339700058 withjson
fi
ten redis-benchmark -n $LOOPR geoencode 0 0
ten redis-benchmark -n $LOOPR geoencode 0 0 withjson
ten redis-benchmark -n $LOOPR geodecode 3471579339700058
ten redis-benchmark -n $LOOPR geodecode 3471579339700058 withjson
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withdistance
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojson
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojsoncollection
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojson withgeojsoncollection
while true; do
ten redis-benchmark -n $LOOPR geoencode 40.7480973 -73.9564142
ten redis-benchmark -n $LOOPR geodecode 1791875796750882
ten redis-benchmark -n $LOOPR geoadd nyc 40.7480973 -73.9564142 4545
ten redis-benchmark -n $LOOPR geoadd nyc 40.7648057 -73.9733487 "central park n/q/r" 40.7362513 -73.9903085 "union square" 40.7126674 -74.0131604 "wtc one" 40.6428986 -73.7858139 "jfk" 40.7498929 -73.9375699 "q4" 40.7480973 -73.9564142 4545
ten redis-benchmark -n $LOOPR georadiusbymember nyc 40.7480973 -73.9564142 5 mi
ten redis-benchmark -n $LOOPR georadiusbymember nyc 40.7480973 -73.9564142 5 mi withgeojson
ten redis-benchmark -n $LOOPR georadiusbymember nyc 40.7480973 -73.9564142 5 mi withgeojson withgeojsoncollection
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withdistance
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojson
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojson withgeojsoncollection withdistance
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojsoncollection withdistance
ten redis-benchmark -n $LOOPR georadiusbymember nyc 4545 20 km withgeojsoncollection
done
