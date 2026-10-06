/*
   Targeted tests for XML_SetEntityExpansionBudget:
     1. a small internal entity expands fine within the budget
     2. exceeding the budget stops parsing and the parser can be freed
        (and reset/reused) safely
     3. an external file entity is rejected up front: the external entity
        handler is never invoked, so no file (or network) access happens
     4. without a budget the original parser behavior is preserved
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expat.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (cond) {                                                                \
      printf("ok   - %s\n", (msg));                                            \
    } else {                                                                   \
      printf("FAIL - %s (%s:%d)\n", (msg), __FILE__, __LINE__);                \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

static char *
read_file(const char *path, size_t *out_len) {
  FILE *const f = fopen(path, "rb");
  char *buf;
  long size;
  if (! f) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(2);
  }
  if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0
      || fseek(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "cannot seek in %s\n", path);
    exit(2);
  }
  buf = (char *)malloc((size_t)size + 1);
  if (! buf) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(2);
  }
  fclose(f);
  buf[size] = '\0';
  *out_len = (size_t)size;
  return buf;
}

static enum XML_Status
parse_doc(XML_Parser parser, const char *path) {
  size_t len = 0;
  char *const doc = read_file(path, &len);
  const enum XML_Status status = XML_Parse(parser, doc, (int)len, XML_TRUE);
  free(doc);
  return status;
}

/* -- handlers ------------------------------------------------------------- */

static char g_text[256];
static size_t g_text_len;

static void XMLCALL
on_character_data(void *userData, const XML_Char *s, int len) {
  (void)userData;
  if (g_text_len + (size_t)len < sizeof(g_text)) {
    memcpy(g_text + g_text_len, s, (size_t)len);
    g_text_len += (size_t)len;
    g_text[g_text_len] = '\0';
  }
}

static unsigned int g_ext_handler_calls = 0;
static unsigned int g_ext_file_opens = 0;

static int XMLCALL
on_external_entity(XML_Parser parser, const XML_Char *context,
                   const XML_Char *base, const XML_Char *systemId,
                   const XML_Char *publicId) {
  /* userData carries the fixtures directory, set via XML_SetUserData */
  const char *const dir = (const char *)XML_GetUserData(parser);
  (void)context;
  (void)base;
  (void)publicId;
  g_ext_handler_calls++;
  /* Prove whether any file access happens through this handler: the
     payload file exists, so a successful open increments
     g_ext_file_opens. */
  if (systemId) {
    char path[512];
    if (strncmp(systemId, "file:///", 8) == 0)
      snprintf(path, sizeof(path), "%s", systemId + 8);
    else
      snprintf(path, sizeof(path), "%s/%s", dir ? dir : ".", systemId);
    {
      FILE *const f = fopen(path, "rb");
      if (f) {
        g_ext_file_opens++;
        fclose(f);
      }
    }
  }
  return XML_STATUS_OK;
}

/* -- tests ---------------------------------------------------------------- */

static void
test_small_internal_entity_ok(const char *dir) {
  char path[512];
  XML_Parser parser;
  snprintf(path, sizeof(path), "%s/internal_ok.xml", dir);

  parser = XML_ParserCreate(NULL);
  CHECK(parser != NULL, "small: parser created");
  CHECK(XML_SetEntityExpansionBudget(parser, 1024) == XML_TRUE,
        "small: budget of 1024 bytes accepted");
  g_text_len = 0;
  g_text[0] = '\0';
  XML_SetCharacterDataHandler(parser, on_character_data);
  CHECK(parse_doc(parser, path) == XML_STATUS_OK,
        "small: internal entity within budget parses successfully");
  CHECK(strcmp(g_text, "Hello, budget!") == 0,
        "small: expanded text is Hello-budget");
  XML_ParserFree(parser);
}

static void
test_budget_exceeded_and_safe_release(const char *dir) {
  char path[512];
  XML_Parser parser;
  snprintf(path, sizeof(path), "%s/internal_over_budget.xml", dir);

  parser = XML_ParserCreate(NULL);
  CHECK(parser != NULL, "over: parser created");
  CHECK(XML_SetEntityExpansionBudget(parser, 100) == XML_TRUE,
        "over: budget of 100 bytes accepted");
  CHECK(parse_doc(parser, path) == XML_STATUS_ERROR,
        "over: parsing stops once the budget is exceeded");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED,
        "over: error is XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED");
  CHECK(XML_ErrorString(XML_ERROR_ENTITY_EXPANSION_BUDGET_EXCEEDED) != NULL,
        "over: error has a message string");

  /* The parser must be safely releasable after the budget abort ... */
  XML_ParserFree(parser);
  CHECK(1, "over: parser freed safely after budget abort");

  /* ... and also reusable after XML_ParserReset. */
  parser = XML_ParserCreate(NULL);
  XML_SetEntityExpansionBudget(parser, 100);
  CHECK(parse_doc(parser, path) == XML_STATUS_ERROR,
        "over: second parser also stops on budget");
  CHECK(XML_ParserReset(parser, NULL) == XML_TRUE,
        "over: parser reset after budget abort");
  CHECK(XML_SetEntityExpansionBudget(parser, 0) == XML_TRUE,
        "over: budget 0 disables the budget again");
  {
    char ok_path[512];
    snprintf(ok_path, sizeof(ok_path), "%s/internal_ok.xml", dir);
    CHECK(parse_doc(parser, ok_path) == XML_STATUS_OK,
          "over: reset parser parses again with budget disabled");
  }
  XML_ParserFree(parser);
}

static void
test_external_file_entity_rejected(const char *dir) {
  char path[512];
  XML_Parser parser;
  snprintf(path, sizeof(path), "%s/external_file.xml", dir);

  g_ext_handler_calls = 0;
  g_ext_file_opens = 0;

  parser = XML_ParserCreate(NULL);
  CHECK(parser != NULL, "external: parser created");
  XML_SetUserData(parser, (void *)dir);
  XML_SetExternalEntityRefHandler(parser, on_external_entity);
  CHECK(XML_SetEntityExpansionBudget(parser, 4096) == XML_TRUE,
        "external: budget accepted");
  CHECK(parse_doc(parser, path) == XML_STATUS_ERROR,
        "external: parsing stops on the external entity");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED,
        "external: error is XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED");
  CHECK(XML_ErrorString(XML_ERROR_EXTERNAL_ENTITY_NOT_ALLOWED) != NULL,
        "external: error has a message string");
  CHECK(g_ext_handler_calls == 0,
        "external: external entity handler never invoked");
  CHECK(g_ext_file_opens == 0, "external: no file read triggered");
  XML_ParserFree(parser);
}

static void
test_default_behavior_unchanged(const char *dir) {
  char path[512];
  XML_Parser parser;

  /* No budget set: external entity handler must be called as before. */
  snprintf(path, sizeof(path), "%s/external_file.xml", dir);
  g_ext_handler_calls = 0;
  g_ext_file_opens = 0;
  parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, (void *)dir);
  XML_SetExternalEntityRefHandler(parser, on_external_entity);
  CHECK(parse_doc(parser, path) == XML_STATUS_OK,
        "default: external entity parses without a budget");
  CHECK(g_ext_handler_calls == 1,
        "default: external entity handler invoked once without a budget");
  CHECK(g_ext_file_opens == 1,
        "default: payload file is readable and was opened once");
  XML_ParserFree(parser);

  /* No budget set: nested internal entities expand as before. */
  snprintf(path, sizeof(path), "%s/internal_over_budget.xml", dir);
  parser = XML_ParserCreate(NULL);
  CHECK(parse_doc(parser, path) == XML_STATUS_OK,
        "default: nested internal entities expand without a budget");
  XML_ParserFree(parser);

  CHECK(XML_SetEntityExpansionBudget(NULL, 10) == XML_FALSE,
        "default: NULL parser rejected");
}

int
main(int argc, char **argv) {
  const char *const dir = (argc > 1) ? argv[1] : "fixtures";

  test_small_internal_entity_ok(dir);
  test_budget_exceeded_and_safe_release(dir);
  test_external_file_entity_rejected(dir);
  test_default_behavior_unchanged(dir);

  if (g_failures == 0) {
    printf("ALL TESTS PASSED\n");
    return 0;
  }
  printf("%d TEST(S) FAILED\n", g_failures);
  return 1;
}
