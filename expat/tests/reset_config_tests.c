/* Local tests for the reset configuration snapshot API
   (XML_ParserConfigSnapshotCreate / XML_ParserConfigSnapshotFree /
   XML_ParserResetWithConfig).

   Scenarios covered:
     1. A failing document is parsed, the parser is reset and the
        snapshot restores handlers and encoding with a NEW userData
        pointer; the old userData pointer is never reused.
     2. Restoring without the required fresh userData is refused and
        a subsequent parse produces zero events (no half-configured
        parsing).
     3. A NULL snapshot is refused the same way.
     4. Plain XML_ParserReset keeps its original behavior of clearing
        all handlers.
     5. Restore options are honored selectively: encoding-only restore
        leaves handlers cleared, handlers-only restore leaves the
        encoding unset.

   All fixtures are small in-memory documents.

   Copyright (c) 2026 Expat development team
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software  and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation  the  rights  to  use,  copy,  modify,  merge,  publish,
   distribute,  sublicense, and/or  sell copies  of  the Software,  and  to
   permit  persons  to whom  the Software  is  furnished  to  do  so,  subject
   to the following conditions:

   The  above  copyright  notice  and  this  permission  notice   shall  be
   included in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR  IMPLIED,  INCLUDING  BUT  NOT  LIMITED  TO  THE  WARRANTIES
   OF  MERCHANTABILITY,  FITNESS  FOR  A  PARTICULAR  PURPOSE  AND  NONINFRIN-
   GEMENT.  IN  NO  EVENT  SHALL  THE  AUTHORS  OR  COPYRIGHT  HOLDERS  BE
   LIABLE  FOR  ANY  CLAIM,  DAMAGES  OR  OTHER  LIABILITY,  WHETHER  IN  AN
   ACTION  OF  CONTRACT,  TORT  OR  OTHERWISE,  ARISING  FROM,  OUT  OF  OR
   IN  CONNECTION  WITH  THE  SOFTWARE  OR  THE  USE  OR  OTHER  DEALINGS IN
   THE SOFTWARE.

   SPDX-License-Identifier: MIT
*/

#include <stdio.h>
#include <string.h>

#include "expat.h"

/* Small in-memory fixtures */

/* Malformed: end tag does not match the open tag. */
static const char DOC_BAD[] = "<root><open></root>";

/* Valid, contains one byte 0xE9: that is e-acute in ISO-8859-1 but an
   invalid UTF-8 token, so this document only parses when the
   ISO-8859-1 protocol encoding is in effect. */
static const char DOC_LATIN1[] = "<d>\xE9</d>";

/* Valid pure-ASCII document. */
static const char DOC_PLAIN[] = "<d>hi</d>";

/* Event log */

typedef struct {
  int startCount;
  int endCount;
  int charCount;
  int commentCount;
  void *lastUserData;
  char charData[16];
  size_t charDataLen;
} EventLog;

static EventLog g_log;

static void
log_reset(void) {
  memset(&g_log, 0, sizeof(g_log));
}

static int
log_event_count(void) {
  return g_log.startCount + g_log.endCount + g_log.charCount
         + g_log.commentCount;
}

static void XMLCALL
on_start_element(void *userData, const XML_Char *name,
                 const XML_Char **atts) {
  (void)name;
  (void)atts;
  g_log.startCount++;
  g_log.lastUserData = userData;
}

static void XMLCALL
on_end_element(void *userData, const XML_Char *name) {
  (void)name;
  g_log.endCount++;
  g_log.lastUserData = userData;
}

static void XMLCALL
on_character_data(void *userData, const XML_Char *s, int len) {
  g_log.charCount++;
  g_log.lastUserData = userData;
  if ((size_t)len < sizeof(g_log.charData)) {
    memcpy(g_log.charData, s, (size_t)len);
    g_log.charDataLen = (size_t)len;
  }
}

static void XMLCALL
on_comment(void *userData, const XML_Char *data) {
  (void)data;
  g_log.commentCount++;
  g_log.lastUserData = userData;
}

/* Minimal check harness */

static int g_failures = 0;
static const char *g_current_test = "";

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (! (cond)) {                                                       \
      printf("FAIL [%s] %s:%d: %s\n", g_current_test, __FILE__, __LINE__, \
             #cond);                                                      \
      g_failures++;                                                       \
    }                                                                     \
  } while (0)

#define RUN_TEST0(fn)                                                  \
  do {                                                                 \
    int before_ = g_failures;                                          \
    g_current_test = #fn;                                              \
    fn();                                                              \
    printf("%s [%s]\n", g_failures == before_ ? "PASS" : "FAIL", #fn); \
  } while (0)

/* Parser factory: handlers + ISO-8859-1 encoding + given userData */

static XML_Parser
make_configured_parser(void *userData) {
  XML_Parser parser = XML_ParserCreate(NULL);
  if (parser == NULL)
    return NULL;
  XML_SetUserData(parser, userData);
  XML_SetElementHandler(parser, on_start_element, on_end_element);
  XML_SetCharacterDataHandler(parser, on_character_data);
  XML_SetCommentHandler(parser, on_comment);
  if (XML_SetEncoding(parser, "ISO-8859-1") != XML_STATUS_OK) {
    XML_ParserFree(parser);
    return NULL;
  }
  return parser;
}

/* Tests (each uses the shared parser/snapshot from main) */

static XML_Parser g_parser;
static XML_ParserConfigSnapshot *g_snap;
static int g_ctxOld = 1;
static int g_ctxNew = 2;

/* Two small documents: the first fails to parse, then reset+restore
   must bring back handlers and encoding and deliver the NEW userData. */
static void
test_restore_after_failed_parse(void) {
  enum XML_Status status;
  XML_Bool ok;

  log_reset();
  status = XML_Parse(g_parser, DOC_BAD, (int)sizeof(DOC_BAD) - 1, XML_TRUE);
  CHECK(status == XML_STATUS_ERROR);
  CHECK(XML_GetErrorCode(g_parser) == XML_ERROR_TAG_MISMATCH);
  CHECK(g_log.lastUserData == &g_ctxOld); /* events used old userData */

  ok = XML_ParserResetWithConfig(g_parser, g_snap, XML_CONFIG_RESTORE_ALL,
                                 &g_ctxNew);
  CHECK(ok == XML_TRUE);
  CHECK(XML_GetUserData(g_parser) == &g_ctxNew);

  log_reset();
  status = XML_Parse(g_parser, DOC_LATIN1, (int)sizeof(DOC_LATIN1) - 1,
                     XML_TRUE);
  /* Parses only if the ISO-8859-1 encoding was restored. */
  CHECK(status == XML_STATUS_OK);
  /* Handlers were restored and received the NEW userData pointer. */
  CHECK(g_log.startCount == 1);
  CHECK(g_log.endCount == 1);
  CHECK(g_log.charCount == 1);
  CHECK(g_log.lastUserData == &g_ctxNew);
  /* 0xE9 in ISO-8859-1 is e-acute, reported to the handler as UTF-8. */
  CHECK(g_log.charDataLen == 2);
  CHECK(g_log.charData[0] == '\xC3' && g_log.charData[1] == '\xA9');
}

/* Restoring without the required fresh userData must be refused and
   must leave the parser fully cleared: zero events on the next parse. */
static void
test_missing_userdata_refused(void) {
  enum XML_Status status;
  XML_Bool ok;

  ok = XML_ParserResetWithConfig(g_parser, g_snap, XML_CONFIG_RESTORE_ALL,
                                 NULL);
  CHECK(ok == XML_FALSE);
  CHECK(XML_GetUserData(g_parser) == NULL);

  log_reset();
  status = XML_Parse(g_parser, DOC_PLAIN, (int)sizeof(DOC_PLAIN) - 1,
                     XML_TRUE);
  CHECK(status == XML_STATUS_OK); /* the document itself is fine ... */
  CHECK(log_event_count() == 0);  /* ... but nothing was dispatched */
}

/* A NULL snapshot must be refused the same way. */
static void
test_null_snapshot_refused(void) {
  enum XML_Status status;
  XML_Bool ok;

  ok = XML_ParserResetWithConfig(g_parser, NULL, XML_CONFIG_RESTORE_ALL,
                                 &g_ctxNew);
  CHECK(ok == XML_FALSE);

  log_reset();
  status = XML_Parse(g_parser, DOC_PLAIN, (int)sizeof(DOC_PLAIN) - 1,
                     XML_TRUE);
  CHECK(status == XML_STATUS_OK);
  CHECK(log_event_count() == 0);
}

/* The original XML_ParserReset is unchanged: it clears all handlers. */
static void
test_plain_reset_unchanged(void) {
  enum XML_Status status;
  XML_Bool ok;

  ok = XML_ParserReset(g_parser, NULL);
  CHECK(ok == XML_TRUE);

  log_reset();
  status = XML_Parse(g_parser, DOC_PLAIN, (int)sizeof(DOC_PLAIN) - 1,
                     XML_TRUE);
  CHECK(status == XML_STATUS_OK);
  CHECK(log_event_count() == 0);
}

/* Option XML_CONFIG_RESTORE_ENCODING alone: encoding restored (the
   latin-1 document parses) but handlers stay cleared (zero events). */
static void
test_encoding_only_option(void) {
  enum XML_Status status;
  XML_Bool ok;

  ok = XML_ParserResetWithConfig(g_parser, g_snap,
                                 XML_CONFIG_RESTORE_ENCODING, &g_ctxNew);
  CHECK(ok == XML_TRUE);
  CHECK(XML_GetUserData(g_parser) == &g_ctxNew);

  log_reset();
  status = XML_Parse(g_parser, DOC_LATIN1, (int)sizeof(DOC_LATIN1) - 1,
                     XML_TRUE);
  CHECK(status == XML_STATUS_OK); /* encoding was restored */
  CHECK(log_event_count() == 0);  /* handlers were not restored */
}

/* Option XML_CONFIG_RESTORE_HANDLERS alone: handlers restored (the
   start-element event fires) but the encoding is unset, so the raw
   0xE9 byte is rejected as invalid UTF-8. */
static void
test_handlers_only_option(void) {
  enum XML_Status status;
  XML_Bool ok;

  ok = XML_ParserResetWithConfig(g_parser, g_snap,
                                 XML_CONFIG_RESTORE_HANDLERS, &g_ctxNew);
  CHECK(ok == XML_TRUE);
  CHECK(XML_GetUserData(g_parser) == &g_ctxNew);

  log_reset();
  status = XML_Parse(g_parser, DOC_LATIN1, (int)sizeof(DOC_LATIN1) - 1,
                     XML_TRUE);
  CHECK(status == XML_STATUS_ERROR); /* no encoding restored */
  CHECK(XML_GetErrorCode(g_parser) == XML_ERROR_INVALID_TOKEN);
  CHECK(g_log.startCount == 1);      /* handlers were restored */
  CHECK(g_log.lastUserData == &g_ctxNew);
}

/* NULL parser handling for the new entry points. */
static void
test_null_parser_arguments(void) {
  XML_Bool ok;
  ok = XML_ParserResetWithConfig(NULL, NULL, XML_CONFIG_RESTORE_ALL,
                                 (void *)"x");
  CHECK(ok == XML_FALSE);
  CHECK(XML_ParserConfigSnapshotCreate(NULL) == NULL);
  XML_ParserConfigSnapshotFree(NULL, NULL); /* must not crash */
}

int
main(void) {
  g_parser = make_configured_parser(&g_ctxOld);
  if (g_parser == NULL) {
    printf("FAIL [setup] could not create configured parser\n");
    return 1;
  }

  g_snap = XML_ParserConfigSnapshotCreate(g_parser);
  if (g_snap == NULL) {
    printf("FAIL [setup] could not create config snapshot\n");
    XML_ParserFree(g_parser);
    return 1;
  }

  /* The snapshot is taken once and reused across all resets below,
     demonstrating that it survives any number of parser resets. */

  RUN_TEST0(test_restore_after_failed_parse);
  RUN_TEST0(test_missing_userdata_refused);
  RUN_TEST0(test_null_snapshot_refused);
  RUN_TEST0(test_plain_reset_unchanged);
  RUN_TEST0(test_encoding_only_option);
  RUN_TEST0(test_handlers_only_option);
  RUN_TEST0(test_null_parser_arguments);

  XML_ParserConfigSnapshotFree(g_parser, g_snap);
  XML_ParserFree(g_parser);

  if (g_failures == 0)
    printf("OK: all reset config snapshot tests passed\n");
  else
    printf("FAILED: %d check(s) failed\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
