# Redis Module Toolkit (krmt) Makefile
#
# The more colorful parts of this Makefile were
# taken from the primary Redis Makefile.
#
# Place a checkout of the Dynamic Redis source tree
# at ../redis or alter REDIS_BASE to point
# to your redis source tree checkout.

REDIS_BASE=../redis
LUA_SRC=$(REDIS_BASE)/deps/lua/src
REDIS_SRC=$(REDIS_BASE)/src

# We include these files directly in our modules so our compiler
# can inline their function bodies when appropriate.
# Including sds.c directly gives us a 5% to 10% throughput boost
# due to clever inlining at compile time.
SDS_C:=$(REDIS_SRC)/sds.c
ZMALLOC_C:=$(REDIS_SRC)/zmalloc.c
NETWORKING_C=$(REDIS_SRC)/networking.c
IMPORT=$(SDS_C) $(ZMALLOC_C) $(NETWORKING_C)

# YAJL is used by json currently, and maybe geo soon.  Let's make
# it a hard-stop requirement for now.
# Note: we include YAJL directly in our modules instead of linking to a
# the yajl library so the compiler can attempt to inline where appropriate.
YAJL_BASE=../yajl
YAJL_S=$(YAJL_BASE)/src
# Hopefully you only have one built yajl version:
YAJL_I=$(dir $(wildcard $(YAJL_BASE)/build/yajl-*/include/))

ifeq ($(wildcard $(YAJL_BASE)/build),)
    $(error You must build YAJL (cd ..;git clone https://github.com/lloyd/yajl;cd yajl;./configure;make) before using krmt)
endif

# For debugging, you may want to back this down to -O0 and remove -DPRODUCTION to
# generate verbose logging in some modules.
CFLAGS:=-O2 -DPRODUCTION -g -Wall -pedantic -std=c99 -Wno-unused-function -Wno-format $(CFLAGS)

# Flags for .so compiling; OS X be crazy
ifeq ($(shell uname -s),Darwin)
	SHARED_FLAGS=-fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
    # Clang supports -flto on OS X 10.9
    CFLAGS:=-flto $(CFLAGS)
    # On OS X, GCC doesn't support march=native, so try to use sse4.2 instead
    ifneq (,$(findstring gcc,$(CC)))
        CFLAGS:=-msse4.2 $(CFLAGS)
    else
        CFLAGS:=-march=native $(CFLAGS)
    endif
else
	SHARED_FLAGS=-fPIC -shared
    # _DEFAULT_SOURCE provides built-in M_ macros for math constants under Linux
    # You can add -flto if you're using GCC >= 4.9 for potential improvements when bulk linking
    CFLAGS:=-march=native -D_DEFAULT_SOURCE $(CFLAGS)
endif

FINAL_CFLAGS:=$(CFLAGS) -I$(LUA_SRC) -I$(REDIS_SRC)

CCCOLOR="\033[34m"
SRCCOLOR="\033[33m"
BINCOLOR="\033[37;1m"
CLEANCOLOR="\033[32;1m"
ENDCOLOR="\033[0m"

ifndef_any_of = $(filter undefined,$(foreach v,$(1),$(origin $(v))))
ifdef_any_of = $(filter-out undefined,$(foreach v,$(1),$(origin $(v))))
# Run with "make VERBOSE=1" if you want full command output
ifeq ($(call ifdef_any_of,VERBOSE V),)
QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)CC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_CLEAN = @printf '    %b %b\n' $(CLEANCOLOR)CLEAN$(ENDCOLOR) $(BINCOLOR)$^$(ENDCOLOR) 1>&2;
endif

.PHONY: all clean
.SUFFIXES: .so

DIRS:=$(filter-out ./, $(dir $(wildcard *[^dSYM]/)))
OBJ:=$(DIRS:/=.so)
OBJ_D:=$(OBJ:%=%.dSYM)

# Yeah, so, this Makefile is more of a "build" file.  It doesn't respond to
# changes in source files for rebuilds.
# You should always "make clean; make -j" when building.
all: $(OBJ)
#$(filter-out  /home/matt/repos/redis/deps/lua/src/lua.c, $(wildcard /home/matt/repos/redis/deps/lua/src/*.c))
%.so: DIRNAME=$*
%.so: $(wildcard %/*.c)
	$(QUIET_CC) $(CC) $(SHARED_FLAGS) $(FINAL_CFLAGS) -I./$(DIRNAME)/ -o $@ $(wildcard $(DIRNAME)/*.c) $(IMPORT)

geo.so: $(wildcard %/*.c)
	$(QUIET_CC) $(CC) $(SHARED_FLAGS) $(FINAL_CFLAGS) -I./$(DIRNAME)/ -o $@ $(filter-out $(DIRNAME)/test.c, $(wildcard $(DIRNAME)/*.c)) $(IMPORT)
	$(QUIET_CC) $(CC) $(FINAL_CFLAGS) -I./$(DIRNAME)/ -o geo-test $(DIRNAME)/geohash.c $(DIRNAME)/test.c

json.so: $(wildcard %/*.c)
	$(QUIET_CC) $(CC) $(SHARED_FLAGS) $(FINAL_CFLAGS) -I$(YAJL_I) -I./$(DIRNAME)/ -o $@ $(wildcard $(DIRNAME)/*.c) $(wildcard $(YAJL_S)/*.c) $(IMPORT)

clean:
	$(QUIET_CLEAN) rm -rf $(OBJ) $(OBJ_D)
