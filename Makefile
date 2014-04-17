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

# Protip: for best performance: compile under clang with -O0 -flto (yes, really)
# _DEFAULT_SOURCE provides built-in M_ macros for math constants under Linux
CFLAGS:=-O2 -g -Wall -pedantic -std=c99 -Wno-unused-function -D_DEFAULT_SOURCE $(CFLAGS)
FINAL_CFLAGS:=$(CFLAGS) -I$(LUA_SRC) -I$(REDIS_SRC)

# Flags for .so compiling; OS X be crazy
ifeq ($(shell uname -s),Darwin)
	SHARED_FLAGS=-fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
else
	SHARED_FLAGS=-fPIC -shared
endif

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

%.so: DIRNAME=$*
%.so: $(wildcard %/*.c)
	$(QUIET_CC) $(CC) $(SHARED_FLAGS) $(FINAL_CFLAGS) -I./$(DIRNAME)/ -o $@ $(wildcard $(DIRNAME)/*.c)

geo.so: $(wildcard %/*.c)
	$(QUIET_CC) $(CC) $(SHARED_FLAGS) $(FINAL_CFLAGS) -I./$(DIRNAME)/ -o $@ $(wildcard $(DIRNAME)/*.c)
	$(QUIET_CC) $(CC) $(FINAL_CFLAGS) -I./$(DIRNAME)/ -o geo-test $(DIRNAME)/geohash.c $(DIRNAME)/test.c

clean:
	$(QUIET_CLEAN) rm -rf $(OBJ) $(OBJ_D)
