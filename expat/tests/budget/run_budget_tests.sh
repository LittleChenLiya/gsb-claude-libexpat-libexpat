#!/bin/sh
# Build and run the directed entity-expansion-budget tests.
# Usage: sh run_budget_tests.sh
set -e
cd "$(dirname "$0")"

BUILD=build
mkdir -p "$BUILD"

# Minimal expat_config.h for a standalone build of the library sources.
cat > "$BUILD/expat_config.h" <<'EOF'
#define XML_GE 1
#define XML_DTD 1
#define XML_CONTEXT_BYTES 1024
EOF

CC=${CC:-cc}
"$CC" -std=c99 -O1 -Wall -Wextra \
  -I"$BUILD" -I../../lib \
  ../../lib/xmlparse.c ../../lib/xmltok.c ../../lib/xmlrole.c ../../lib/xcs.c ../../lib/random_rand_s.c \
  budget_tests.c -o "$BUILD/budget_tests"

"$BUILD/budget_tests" fixtures
