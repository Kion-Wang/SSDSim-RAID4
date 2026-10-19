CC = gcc
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D__USE_MINGW_ANSI_STDIO=1
CFLAGS ?= -std=gnu99 -O2 -g
LDLIBS += -lm
SOURCES = ssd.c flash.c pagemap.c hash.c initialize.c raid4.c
OBJECTS = $(SOURCES:.c=.o)
ifeq ($(OS),Windows_NT)
EXEEXT = .exe
endif
.PHONY: all clean test
all: ssd$(EXEEXT)
ssd$(EXEEXT): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
%.o: %.c initialize.h ssd.h flash.h pagemap.h hash.h raid4.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
clean:
	$(RM) ssd ssd.exe $(OBJECTS)
test:
	python tests/test_raid4.py
