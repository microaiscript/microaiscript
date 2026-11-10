# Third-party notices

MicroAiScript itself is MIT licensed (see [LICENSE](LICENSE)). It incorporates
or links to the following third-party material.

| Component | Where | License |
|---|---|---|
| cJSON 1.7.19 | `lib/cJSON.c`, `lib/cJSON.h` (conformance runner only) | MIT, see [lib/LICENSE.cJSON](lib/LICENSE.cJSON) |
| AiScript language reference | `docs/reference/` (English documentation adapted from the upstream project) | MIT, Copyright (c) 2021-2024 syuilo, <https://github.com/aiscript-dev/aiscript> |
| AiScript conformance cases | `test/conformance/*.json` (extracted from `aiscript-rs`) | See <https://github.com/poppingmoon/aiscript-rs> |
| libmicrohttpd2 | Not distributed. Downloaded by `modules/http-server/fetch-mhd2.sh` and linked dynamically by the optional `http_server.so` module | LGPL-2.1-or-later OR GPL-2.0-or-later with the eCos exception |
| libcurl | Not distributed. Linked by the optional `http_client.so` module | curl license |
| GTK 3 | Not distributed. Linked by the optional `ui_gtk.so` example module | LGPL-2.1-or-later |
