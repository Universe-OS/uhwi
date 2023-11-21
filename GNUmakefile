CC = clang
CFLAGS := -Wall -Werror -std=c99 -I. $(CFLAGS)

AR ?= ar

TARGET = libuhwi.a
TARGET_BIN = lsuhwi

TARGETS = uhwi.o
TARGETS_BIN = lsuhwi.o

ifeq ($(shell uname),Darwin)
TARGETS += uhwi_macos.o
LIBS := $(LIBS) -framework IOKit -framework CoreFoundation
endif

ifeq ($(shell uname),FreeBSD)
LIBS := $(LIBS) -lusb
endif

ifdef DEBUG
CFLAGS += -g
endif

all: lib bin

lib: $(TARGET)
bin: $(TARGET_BIN)

$(TARGET): $(TARGETS)
	$(AR) crs $(TARGET) $(TARGETS)

$(TARGET_BIN): $(TARGET) $(TARGETS_BIN)
	$(CC) $(LDFLAGS) -o $(TARGET_BIN) $(TARGETS_BIN) -L. -luhwi $(LIBS)

$(TARGETS) $(TARGETS_BIN):
	$(CC) -c -o "$@" $(CFLAGS) "$(shell basename "$@" .o).c"

clean: distclean

distclean:
	-rm -rf *.dSYM
	-rm -f $(TARGETS_BIN) $(TARGETS) $(TARGET_BIN) $(TARGET)
