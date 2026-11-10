# HTTP client native module (libcurl). Load with: import "./http_client.so"
# Prerequisite: libcurl development package (e.g. libcurl4-openssl-dev on Debian).
#
# From this directory:
#   make
# produces http_client.so (use with aiscript linked with -rdynamic, or link against libaiscript).

CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -fPIC -O2 -I../../include
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)
ifeq ($(CURL_LIBS),)
  CURL_LIBS = -lcurl
endif

TARGET = http_client.so
SOURCES = http_curl.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -shared -o $@ $^ $(CURL_LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
