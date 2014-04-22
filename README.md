krmt: Kit of Redis Module Tools
===============================

About
-----
Welcome to a kit of tools you can easily add to Redis.

This repository gives you multiple ready-to-use Redis Add-on Modules.

You can also use the modules here for examples and templates to create
your own your own loadable Redis modules.

**Note:** You must use a
[Dynamic Redis](https://matt.sh/dynamic-redis) server if you
want to use modules.  Regular Redis doesn't know about modules.

Disclaimers:

  - Redis is single threaded for all command processing.
  - While Redis is inside your command function, nothing else runs.
  - Your command should operate and return as quickly as possible.  See existing
  Redis commands in the main `redisCommandTable[]` at the top of redis.c for
  examples of how to write different Redis commands.
  - Make sure to release objects and free memory you allocate. You will
  introduce memory leaks if you forget to free things.
  - You don't have to write in C. You can use C, C++, Go (?), Guile, Chicken, or anything else
  generating a shared library with C symbols for your operating system.  You
  can even link additional libraries (want numerical routines?  embedded
  sqlite3?  embedded snappy/zippy?)


Writing Your Module
-------------------
To write your module: start out with the simple module provided in this repository
and modify commands, functions, fields, and structs as needed.

You will need to study the existing Redis source to learn how existing Redis
commands work.

Also see the notes at [Dynamic Redis: Use Command Modules](https://matt.sh/dynamic-redis#_use-command-modules).

Bundled Commands
----------------
Included in `krmt`:
  - `geo.so` - provides efficient geographic coordinate range searches
    - See [Redis Geo Commands](https://matt.sh/redis-geo) for usage details.
  - `hmsetnx.so` - provides `HMSETNX` (from a [pull request](https://github.com/antirez/redis/issues/542)
    and updated to integrate into more recent Redis versions).
  - `bitallpos.so` - provides `BITALLPOS` command returning the positions
of all set bits in a string (from a [pull request](https://github.com/antirez/redis/pull/1295)).
  - `scriptname.so` - provides `SCRIPTNAME` and `EVALNAME` commands allowing you
to bind user friendly names to loaded script SHA hashes, then you can call
scripts by name (using `EVALNAME`) instead of by a 40 character long hash
reference.
    - Right now, this module has the best comment structure and best design
    patterns for creating your own modules.
    - Use the `dynamic-redis-unstable` branch to build `scriptname.c` since
    `scriptname.c` depends on a header not exported on released versions yet.
  - `poong.so` - minimal module showing how the basic Dynamic Redis interface
API works.

Building
--------
For building, you need a copy of the Dynamic Redis source tree.

If you want to build against the most recent 2.8 commits, use:

```haskell
mkdir -p ~/repos
cd ~/repos
git clone https://github.com/mattsta/redis
cd redis
git checkout dynamic-redis-2.8
cd ..
git clone https://github.com/mattsta/krmt
cd krmt
make clean; make -j
```

If you want to build against the current development branch,
just change `dynamic-redis-2.8` to `dynamic-redis-unstable`.

Usage
-----
After building your module, you can load it into [Dynamic Redis](https://matt.sh/dynamic-redis).

Compatability
-------------
As Redis adds or removes features, sometimes modules may need to
be updated to take into account different functions or interfaces
available to them.

Stable releases of Dynamic Redis (as of 0.5.2) define a few
`DYN_FEATURE_[feature]` constants you can use for preprocessor selective
including or excluding of code.

The currently defined features are negative features allowing the absense
of them to mean "use behavior from unstable branch."

You can check all the current feature defines in `version.h` of your
Dynamic Redis checkout.

For the unstable branch, no features are defined.

Automatic Module Reloading
--------------------------
During development, you can automatically reload your
modules using some simple loops.  Just run these
from the directory where you compile your modules
and everything should work.

Linux:
```bash
while inotifywait -e modify module.so; do
    redis-cli config set module-add `pwd`/module.so
done
```

OS X:
```bash
brew install kqwait
while kqwait module.so; do
    redis-cli config set module-add `pwd`/module.so
done
```

Tips
----
If you are on OS X, you should monitor your redis-server for
new memory leaks by running `leaks` in a persistent terminal window:
```bash
watch "leaks redis-server"
```

Think of `leaks` as `valgrind`, except it can monitor leaks in any
live process.  We run `leaks` in a `watch` loop to refresh the
check fairly often.

If you are on Linux, you should run your module tests under
`valgrind` after a good day of development.  Any changes should
also be re-evaluated for memory leaks and invalid memory access.

Perfecting a Redis command takes time and understanding.  Using
memory allocation debuggers will help you eventually internalize
the complexities of how and when Redis uses reference counted objects
and malloc'd strings for certain operations.

Sample Output
-------------
Sample output on startup on OS X (Linux will complain if you give it a module name without a path):
```c
matt@ununoctium:~/repos/redis/src% ./redis-server --module-strict no --module-add ping.so
[81021] 28 Mar 14:43:14.500 * Module [<builtin>] loaded 145 commands.
[81021] 28 Mar 14:43:14.501 * Loading new [ping.so] module.
[81021] 28 Mar 14:43:14.501 # [ping.so] version mismatch. Module was compiled against 2.9.11, but we are 2.8.8-dynamic-0.5. 
[81021] 28 Mar 14:43:14.501 # Strict version enforcement is disabled. Loading [ping.so] but undefined behavior may occur.
[81021] 28 Mar 14:43:14.501 * Added command poong [ping.so]
[81021] 28 Mar 14:43:14.501 * Added command pooooong [ping.so]
[81021] 28 Mar 14:43:14.501 * Added command pooong [ping.so]
[81021] 28 Mar 14:43:14.501 * Added command pinger [ping.so]
[81021] 28 Mar 14:43:14.501 * Module [ping.so] loaded 4 commands.
[81021] 28 Mar 14:43:14.501 * Running load function of module [ping.so].
```

Module reloaded with: `CONFIG SET module-add /Users/matt/repos/krmt/poong.so`

Sample output on reloading a module (using a different filename, but the same module name):
```c
[81021] 28 Mar 14:43:29.796 * Running cleanup function for [ping.so] module.
[81021] 28 Mar 14:43:29.796 * Closed previous [ping.so] module.
[81021] 28 Mar 14:43:29.796 * Loading new [/Users/matt/repos/krmt/poong.so] module.
[81021] 28 Mar 14:43:29.796 * Replaced existing command poong [/Users/matt/repos/krmt/poong.so]
[81021] 28 Mar 14:43:29.796 * Replaced existing command pooooong [/Users/matt/repos/krmt/poong.so]
[81021] 28 Mar 14:43:29.796 * Replaced existing command pooong [/Users/matt/repos/krmt/poong.so]
[81021] 28 Mar 14:43:29.796 * Replaced existing command pinger [/Users/matt/repos/krmt/poong.so]
[81021] 28 Mar 14:43:29.796 * Module [/Users/matt/repos/krmt/poong.so] loaded 4 commands.
[81021] 28 Mar 14:43:29.796 * Running load function of module [/Users/matt/repos/krmt/poong.so].
```

Sample `INFO modules` output:
```c
module_<builtin>:filename=<builtin>,compiled_against=2.8.8-dynamic-0.5,
module_version=2.8.8-dynamic-0.5,first_loaded=0,last_loaded=0,commands=get,set,
setnx,setex,psetex,append,strlen,del,exists,setbit,getbit,setrange,getrange,
substr,incr,decr,mget,rpush,lpush,rpushx,lpushx,linsert,rpop,lpop,brpop,
brpoplpush,blpop,llen,lindex,lset,lrange,ltrim,lrem,rpoplpush,sadd,srem,smove,
sismember,scard,spop,srandmember,sinter,sinterstore,sunion,sunionstore,sdiff,
sdiffstore,smembers,sscan,zadd,zincrby,zrem,zremrangebyscore,zremrangebyrank,
zunionstore,zinterstore,zrange,zrangebyscore,zrevrangebyscore,zcount,zrevrange,
zcard,zscore,zrank,zrevrank,zscan,hset,hsetnx,hget,hmset,hmget,hincrby,
hincrbyfloat,hdel,hlen,hkeys,hvals,hgetall,hexists,hscan,incrby,decrby,
incrbyfloat,getset,mset,msetnx,randomkey,select,move,rename,renamenx,expire,
expireat,pexpire,pexpireat,keys,scan,dbsize,auth,ping,echo,save,bgsave,
bgrewriteaof,shutdown,lastsave,type,multi,exec,discard,sync,psync,replconf,
flushdb,flushall,sort,info,monitor,ttl,pttl,persist,slaveof,debug,config,
subscribe,unsubscribe,psubscribe,punsubscribe,publish,pubsub,watch,unwatch,
restore,migrate,dump,object,client,eval,evalsha,slowlog,script,time,bitop,
bitcount,bitpos
module_sh.matt.test.pong:filename=/Users/matt/repos/krmt/poong.so,
compiled_against=2.8.8-dynamic-0.5,module_version=0.0001,
first_loaded=0,last_loaded=1396032209,commands=poong,pooooong,pooong,pinger
```

Contributions
-------------
Did you make a nice Redis Add-On Module?  Does it have a beginning,
a middle, and an end?  Got a protagonist with some obstacles to overcome?

Just open a pull request against this repository if you want
to share your modules and get them added to `krmt`.

Modules are more likely to be accepted if they have meaningful comments,
don't leak memory, don't crash Redis, and show us something new and
delightful while solving problems we've had forever (or solving problems
we didn't even know we had).
