# HTTP server native module (libmicrohttpd2). Load with: import "./http_server.so"
#
# libmicrohttpd2 is not vendored. Fetch, patch and build it once with:
#   ./fetch-mhd2.sh
# (pins a commit and applies patches/*.patch; the patch adds
# MHD_daemon_process_blocking(), which the upstream header declares but the
# shipped sources omit). Then:
#   make
# produces http_server.so. At runtime, set LD_LIBRARY_PATH to
# libmicrohttpd2/src/mhd2/.libs if the loader does not find libmicrohttpd2.so,
# or copy/symlink that directory next to the .so (an rpath is also embedded).

CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -fPIC -O2 -I../../include \
	-Ilibmicrohttpd2/src/include

MHD2_DIR = libmicrohttpd2
MHD2_LIBDIR = $(MHD2_DIR)/src/mhd2/.libs
MHD2_LIB = $(MHD2_LIBDIR)/libmicrohttpd2.so

TARGET = http_server.so
SOURCES = http_mhd2.c

LDFLAGS_RPATH = -Wl,-rpath,'$(abspath $(MHD2_LIBDIR))'

all: $(TARGET)

$(MHD2_LIB):
	@echo "libmicrohttpd2 not built; run ./fetch-mhd2.sh first" >&2; exit 1

$(TARGET): $(SOURCES) $(MHD2_LIB)
	$(CC) $(CFLAGS) -shared -o $@ $(SOURCES) -L$(MHD2_LIBDIR) -lmicrohttpd2 \
		$(LDFLAGS_RPATH) -pthread

clean:
	rm -f $(TARGET)

.PHONY: all clean
