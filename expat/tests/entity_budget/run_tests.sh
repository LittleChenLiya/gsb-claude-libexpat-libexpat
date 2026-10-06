#!/bin/sh
# Build and run the targeted entity-expansion-budget tests.
# Usage: sh run_tests.sh
set -e
cd "$(dirname "$0")"

BUILD=build
mkdir -p "$BUILD"

# Minimal expat_config.h for a native general-entities build
cat > "$BUILD/expat_config.h" <<'CFG'
#define XML_GE 1
#define XML_DTD 1
#define XML_NS 1
#define XML_CONTEXT_BYTES 1024
#define HAVE_MEMMOVE 1
#define HAVE_STDINT_H 1
#define HAVE_STDDEF_H 1
CFG

CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
"$CC" -std=c99 -Wall -Wextra -O1 \
  -I "$BUILD" -I ../../lib \
  budget_tests.c ../../lib/xmlparse.c ../../lib/xmlrole.c ../../lib/xmltok.c \
  ../../lib/xcs.c ../../lib/random_rand_s.c \
  -o "$BUILD/budget_tests"

"$BUILD/budget_tests" fixtures
