CC = gcc
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D__USE_MINGW_ANSI_STDIO=1
CFLAGS ?= -std=gnu99 -O2 -g
LDLIBS += -lm
SOURCES = ssd.c flash.c pagemap.c hash.c initialize.c raid4.c parity_policy.c bfp_cache.c experiment_stats.c
OBJECTS = $(SOURCES:.c=.o)
ifeq ($(OS),Windows_NT)
EXEEXT = .exe
endif
.PHONY: all clean test test-bfp test-stats
all: ssd$(EXEEXT)
ssd$(EXEEXT): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
%.o: %.c initialize.h ssd.h flash.h pagemap.h hash.h raid4.h parity_policy.h bfp_cache.h experiment_stats.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
clean:
	$(RM) ssd ssd.exe $(OBJECTS)
test:
	python tests/test_raid4.py
	python tests/test_bfp.py --no-build
	python tests/test_experiment_stats.py --no-build
test-bfp:
	python tests/test_bfp.py
test-stats:
	python tests/test_experiment_stats.py
