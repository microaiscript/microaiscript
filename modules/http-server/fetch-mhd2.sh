#!/bin/sh
# Fetch libmicrohttpd2 at a pinned commit, apply the local patch(es) from
# patches/, and build it. Run from any directory:
#
#   modules/http-server/fetch-mhd2.sh
#
# The result lives in modules/http-server/libmicrohttpd2/ (git-ignored).
# libmicrohttpd2 is licensed LGPL-2.1-or-later OR GPL-2.0-or-later with the
# eCos exception; http_server.so links to it dynamically.
set -eu

MHD2_URL="git://git.gnunet.org/libmicrohttpd2.git"
MHD2_COMMIT="57a987c8e31b77ec6270a97a900e951f8bb24593"

here=$(cd "$(dirname "$0")" && pwd)
dest="$here/libmicrohttpd2"

if [ -d "$dest" ]; then
    echo "fetch-mhd2: $dest already exists; remove it to fetch again" >&2
    exit 1
fi

mkdir -p "$dest"
cd "$dest"
git init -q .
git remote add origin "$MHD2_URL"
git fetch -q --depth 1 origin "$MHD2_COMMIT"
git checkout -q FETCH_HEAD

for p in "$here"/patches/*.patch; do
    echo "fetch-mhd2: applying $(basename "$p")"
    git apply "$p"
done

[ -x ./configure ] || ./autogen.sh
./configure --disable-doc --disable-examples --with-mbedtls=no
make -j"$(nproc 2>/dev/null || echo 2)"
