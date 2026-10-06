/*
 * Directed tests for the entity expansion budget entry point
 * (XML_SetEntityExpansionBudget / XML_GetEntityExpansionCount).
 *
 * Coverage:
 *   1. small internal entity within budget parses successfully
 *   2. internal entity expansion beyond budget stops parsing with
 *      XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED and the parser can
 *      be safely freed (and reset) afterwards
 *   3. external file entity is rejected with
 *      XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED without invoking the
 *      external entity handler, i.e. without any file access
 *   4. a parser without a budget keeps the default behavior
 *
 * This is a standalone test program; it is not part of the expat
 * test suite.  Build and run via run_budget_tests.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expat.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (! (cond)) {                                                            \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

static char g_fixturesDir[1024] = "fixtures";

static char *
read_fixture(const char *name, size_t *outLen) {
  char path[1200];
  FILE *f;
  long size;
  char *buf;

  snprintf(path, sizeof(path), "%s/%s", g_fixturesDir, name);
  f = fopen(path, "rb");
  if (! f) {
    fprintf(stderr, "cannot open %s\n", path);
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0
      || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  buf = (char *)malloc((size_t)size + 1);
  if (! buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  buf[size] = '\0';
  if (outLen)
    *outLen = (size_t)size;
  return buf;
}

/* Counts invocations; reading the file here proves whether the
 * external entity was actually fetched. */
static int g_extHandlerCalls = 0;
static int g_extHandlerReadOk = 0;

static int XMLCALL
external_entity_handler(XML_Parser parser, const XML_Char *context,
                        const XML_Char *base, const XML_Char *systemId,
                        const XML_Char *publicId) {
  XML_Parser extParser;
  size_t len = 0;
  char *content;
  enum XML_Status status;

  (void)base;
  (void)publicId;

  g_extHandlerCalls++;

  content = read_fixture(systemId, &len);
  if (! content)
    return XML_STATUS_ERROR;
  g_extHandlerReadOk = 1;

  extParser = XML_ExternalEntityParserCreate(parser, context, NULL);
  if (! extParser) {
    free(content);
    return XML_STATUS_ERROR;
  }
  status = XML_Parse(extParser, content, (int)len, XML_TRUE);
  XML_ParserFree(extParser);
  free(content);
  return status == XML_STATUS_OK ? XML_STATUS_OK : XML_STATUS_ERROR;
}

static char g_chardata[256];
static size_t g_chardataLen = 0;

static void XMLCALL
chardata_handler(void *userData, const XML_Char *s, int len) {
  (void)userData;
  if (g_chardataLen + (size_t)len < sizeof(g_chardata)) {
    memcpy(g_chardata + g_chardataLen, s, (size_t)len);
    g_chardataLen += (size_t)len;
    g_chardata[g_chardataLen] = '\0';
  }
}

static enum XML_Status
parse_fixture(XML_Parser parser, const char *name) {
  size_t len = 0;
  char *doc = read_fixture(name, &len);
  enum XML_Status status;
  if (! doc)
    return XML_STATUS_ERROR;
  status = XML_Parse(parser, doc, (int)len, XML_TRUE);
  free(doc);
  return status;
}

/* 1. Small internal entity, well within budget: must succeed, and the
 *    accounted bytes must equal the actually expanded bytes
 *    (2 references x 10 bytes of replacement text). */
static void
test_small_internal_within_budget(void) {
  XML_Parser parser = XML_ParserCreate(NULL);
  enum XML_Status status;

  CHECK(parser != NULL, "XML_ParserCreate");
  CHECK(XML_SetEntityExpansionBudget(parser, 1024) == XML_TRUE,
        "XML_SetEntityExpansionBudget(1024)");

  status = parse_fixture(parser, "small_internal.xml");
  CHECK(status == XML_STATUS_OK, "small internal entity parses within budget");
  if (status != XML_STATUS_OK)
    fprintf(stderr, "  unexpected error: %s\n",
            XML_ErrorString(XML_GetErrorCode(parser)));
  CHECK(XML_GetEntityExpansionCount(parser) == 20,
        "expanded bytes accounted as 2 x 10 = 20");

  XML_ParserFree(parser);
  printf("ok: small internal entity within budget\n");
}

/* 2. Two 60-byte expansions against a budget of 100: the second
 *    expansion must stop the parse, and the parser must remain safe
 *    to free (and to reset for reuse with default behavior). */
static void
test_over_budget_stops_and_frees(void) {
  XML_Parser parser = XML_ParserCreate(NULL);
  enum XML_Status status;

  CHECK(parser != NULL, "XML_ParserCreate");
  CHECK(XML_SetEntityExpansionBudget(parser, 100) == XML_TRUE,
        "XML_SetEntityExpansionBudget(100)");

  status = parse_fixture(parser, "over_budget.xml");
  CHECK(status == XML_STATUS_ERROR, "over-budget parse fails");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED,
        "error is XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED");
  CHECK(XML_GetEntityExpansionCount(parser) == 60,
        "only the first 60-byte expansion was accounted");

  /* Safe release after budget breach. */
  XML_ParserFree(parser);

  /* Reset clears the budget: default behavior is available again. */
  parser = XML_ParserCreate(NULL);
  CHECK(XML_SetEntityExpansionBudget(parser, 100) == XML_TRUE, "set budget");
  CHECK(XML_ParserReset(parser, NULL) == XML_TRUE, "XML_ParserReset");
  status = parse_fixture(parser, "over_budget.xml");
  CHECK(status == XML_STATUS_OK,
        "after reset, same document parses without budget");
  XML_ParserFree(parser);

  printf("ok: over-budget parse stops and parser is safely released\n");
}

/* 3. External file entity must be rejected at the budget entry point:
 *    the external entity handler must not run, so no file is read. */
static void
test_external_file_entity_rejected(void) {
  XML_Parser parser = XML_ParserCreate(NULL);
  enum XML_Status status;

  g_extHandlerCalls = 0;
  g_extHandlerReadOk = 0;

  CHECK(parser != NULL, "XML_ParserCreate");
  CHECK(XML_SetEntityExpansionBudget(parser, 4096) == XML_TRUE,
        "XML_SetEntityExpansionBudget(4096)");
  XML_SetExternalEntityRefHandler(parser, external_entity_handler);

  status = parse_fixture(parser, "external_file.xml");
  CHECK(status == XML_STATUS_ERROR, "external entity parse fails");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED,
        "error is XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED");
  CHECK(g_extHandlerCalls == 0,
        "external entity handler never invoked (no file access)");
  CHECK(g_extHandlerReadOk == 0, "external file was never read");

  XML_ParserFree(parser);
  printf("ok: external file entity rejected without file access\n");
}

/* 4. Without a budget the default behavior is preserved: the external
 *    entity handler runs and the file content reaches the document. */
static void
test_default_behavior_unchanged(void) {
  XML_Parser parser = XML_ParserCreate(NULL);
  enum XML_Status status;

  g_extHandlerCalls = 0;
  g_extHandlerReadOk = 0;
  g_chardataLen = 0;
  g_chardata[0] = '\0';

  CHECK(parser != NULL, "XML_ParserCreate");
  XML_SetExternalEntityRefHandler(parser, external_entity_handler);
  XML_SetCharacterDataHandler(parser, chardata_handler);

  status = parse_fixture(parser, "external_file.xml");
  CHECK(status == XML_STATUS_OK, "default parser accepts external entity");
  CHECK(g_extHandlerCalls == 1, "external entity handler invoked once");
  CHECK(g_extHandlerReadOk == 1, "external file read by default parser");
  CHECK(strstr(g_chardata, "external entity payload") != NULL,
        "external entity content reached the document");

  XML_ParserFree(parser);
  printf("ok: default parser behavior unchanged\n");
}

/* 5. Robustness of the new entry points. */
static void
test_null_parser_arguments(void) {
  CHECK(XML_SetEntityExpansionBudget(NULL, 1) == XML_FALSE,
        "XML_SetEntityExpansionBudget(NULL, ...) fails");
  CHECK(XML_GetEntityExpansionCount(NULL) == 0,
        "XML_GetEntityExpansionCount(NULL) is 0");
  printf("ok: NULL parser arguments handled\n");
}

int
main(int argc, char **argv) {
  if (argc > 1) {
    snprintf(g_fixturesDir, sizeof(g_fixturesDir), "%s", argv[1]);
  }

  test_small_internal_within_budget();
  test_over_budget_stops_and_frees();
  test_external_file_entity_rejected();
  test_default_behavior_unchanged();
  test_null_parser_arguments();

  if (g_failures) {
    fprintf(stderr, "FAILED: %d check(s) failed\n", g_failures);
    return 1;
  }
  printf("PASS: all budget tests passed\n");
  return 0;
}
