/* Targeted tests for XML_StopParser / XML_ResumeParser event receipts.

   These tests pin down the suspend/resume contract for a single parser
   fed with small chunks of XML:

   1. Suspending at the second start-element event keeps the remaining
      input with the current parser; XML_ResumeParser publishes only the
      remaining events, exactly once.
   2. Character data split across chunk boundaries is not published
      twice when the parser is suspended and resumed.
   3. Resuming a parser that was terminated (XML_StopParser with
      resumable == XML_FALSE) -- or that was never suspended -- fails
      with a status error.

   Nested (external entity) parsers are intentionally out of scope.
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2026      Expat maintainers
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.

   SPDX-License-Identifier: MIT
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expat.h"

/* ------------------------------------------------------------------
 * Event receipt log: every handler appends a receipt to one string,
 *   S(name)   start-element event
 *   E(name)   end-element event
 *   C(text)   character-data event
 * so tests can compare the exact published event sequence.
 * ------------------------------------------------------------------ */

typedef struct {
  XML_Parser parser;
  char log[4096];
  size_t logLen;
  char text[1024]; /* character data only, concatenated */
  size_t textLen;
  int startCount;
  int suspendAtStart; /* suspend at Nth start event; 0 = never */
  int suspendOnNextCharData;
  enum XML_Error handlerError;
} ReceiptLog;

static void
receipt_append(ReceiptLog *rl, const char *str) {
  const size_t n = strlen(str);
  if (rl->logLen + n + 1 > sizeof(rl->log)) {
    fprintf(stderr, "event receipt log overflow\n");
    exit(2);
  }
  memcpy(rl->log + rl->logLen, str, n + 1);
  rl->logLen += n;
}

static size_t
xmlchars_len(const XML_Char *s) {
  size_t n = 0;
  while (s[n] != 0)
    n++;
  return n;
}

static void
receipt_append_xmlchars(ReceiptLog *rl, const XML_Char *s, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    /* All fixtures are ASCII, so narrowing XML_Char is lossless here. */
    const char c = (char)s[i];
    if (rl->logLen + 2 > sizeof(rl->log)) {
      fprintf(stderr, "event receipt log overflow\n");
      exit(2);
    }
    rl->log[rl->logLen++] = c;
    rl->log[rl->logLen] = '\0';
  }
}

static void XMLCALL
receipt_start_element(void *userData, const XML_Char *name,
                      const XML_Char **atts) {
  ReceiptLog *const rl = (ReceiptLog *)userData;
  (void)atts;

  receipt_append(rl, "S(");
  receipt_append_xmlchars(rl, name, xmlchars_len(name));
  receipt_append(rl, ")");

  rl->startCount++;
  if (rl->suspendAtStart == rl->startCount) {
    if (XML_StopParser(rl->parser, XML_TRUE) != XML_STATUS_OK)
      rl->handlerError = XML_GetErrorCode(rl->parser);
  }
}

static void XMLCALL
receipt_end_element(void *userData, const XML_Char *name) {
  ReceiptLog *const rl = (ReceiptLog *)userData;

  receipt_append(rl, "E(");
  receipt_append_xmlchars(rl, name, xmlchars_len(name));
  receipt_append(rl, ")");
}

static void XMLCALL
receipt_char_data(void *userData, const XML_Char *s, int len) {
  ReceiptLog *const rl = (ReceiptLog *)userData;
  size_t i;

  receipt_append(rl, "C(");
  receipt_append_xmlchars(rl, s, (size_t)len);
  receipt_append(rl, ")");

  for (i = 0; i < (size_t)len; i++) {
    if (rl->textLen + 2 > sizeof(rl->text)) {
      fprintf(stderr, "character data buffer overflow\n");
      exit(2);
    }
    rl->text[rl->textLen++] = (char)s[i];
    rl->text[rl->textLen] = '\0';
  }

  if (rl->suspendOnNextCharData) {
    rl->suspendOnNextCharData = 0;
    if (XML_StopParser(rl->parser, XML_TRUE) != XML_STATUS_OK)
      rl->handlerError = XML_GetErrorCode(rl->parser);
  }
}

static void XMLCALL
aborting_start_element(void *userData, const XML_Char *name,
                       const XML_Char **atts) {
  XML_Parser parser = (XML_Parser)userData;
  (void)name;
  (void)atts;
  XML_StopParser(parser, XML_FALSE); /* terminate, not resumable */
}

/* ------------------------------------------------------------------ */

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (! (cond)) {                                                            \
      fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);          \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

static void
check_log(const ReceiptLog *rl, const char *expected, const char *msg) {
  if (strcmp(rl->log, expected) != 0) {
    fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", msg,
            expected, rl->log);
    g_failures++;
  }
}

/* Suspend at the second start event; resume must publish only the
   remaining events, exactly once, from the input the parser already has. */
static void
test_suspend_at_second_start_event(void) {
  const char *const doc = "<doc><a>hi</a><b/></doc>";
  ReceiptLog rl;
  enum XML_Status status;

  memset(&rl, 0, sizeof(rl));
  rl.parser = XML_ParserCreate(NULL);
  rl.suspendAtStart = 2; /* suspend inside the start handler for <a> */
  XML_SetUserData(rl.parser, &rl);
  XML_SetElementHandler(rl.parser, receipt_start_element,
                        receipt_end_element);
  XML_SetCharacterDataHandler(rl.parser, receipt_char_data);

  status = XML_Parse(rl.parser, doc, (int)strlen(doc), XML_TRUE);
  CHECK(status == XML_STATUS_SUSPENDED,
        "parse did not suspend at second start event");
  CHECK(rl.handlerError == XML_ERROR_NONE,
        "XML_StopParser failed inside handler");
  check_log(&rl, "S(doc)S(a)",
            "events published before suspend must be exactly the first two "
            "start events");

  /* The remaining input still belongs to this parser: resuming must
     publish the rest of the events without re-publishing anything. */
  status = XML_ResumeParser(rl.parser);
  CHECK(status == XML_STATUS_OK, "resume after suspend failed");
  check_log(&rl, "S(doc)S(a)C(hi)E(a)S(b)E(b)E(doc)",
            "resume must publish only the remaining events, once each");
  CHECK(strcmp(rl.text, "hi") == 0, "character data mismatch after resume");

  XML_ParserFree(rl.parser);
}

/* Character data crossing chunk boundaries must not be duplicated when
   the parser is suspended in the character-data handler and resumed. */
static void
test_resume_no_duplicate_chardata_across_chunks(void) {
  const char *const doc = "<r>hello cross-chunk text</r>";
  const size_t docLen = strlen(doc);
  const size_t chunkSize = 4;
  ReceiptLog rl;
  size_t offset = 0;
  int sawSuspend = 0;

  memset(&rl, 0, sizeof(rl));
  rl.parser = XML_ParserCreate(NULL);
  rl.suspendOnNextCharData = 1; /* suspend inside first chardata call */
  XML_SetUserData(rl.parser, &rl);
  XML_SetElementHandler(rl.parser, receipt_start_element,
                        receipt_end_element);
  XML_SetCharacterDataHandler(rl.parser, receipt_char_data);
  /* Disable reparse deferral so each small chunk is parsed as fed. */
  if (XML_SetReparseDeferralEnabled(rl.parser, XML_FALSE) != XML_STATUS_OK) {
    fprintf(stderr, "FAIL: XML_SetReparseDeferralEnabled rejected\n");
    g_failures++;
  }

  while (offset < docLen) {
    const size_t left = docLen - offset;
    const int chunk = (int)(left < chunkSize ? left : chunkSize);
    const int isFinal = (offset + (size_t)chunk == docLen) ? XML_TRUE
                                                           : XML_FALSE;
    enum XML_Status status
        = XML_Parse(rl.parser, doc + offset, chunk, isFinal);
    offset += (size_t)chunk;

    if (status == XML_STATUS_SUSPENDED) {
      sawSuspend = 1;
      status = XML_ResumeParser(rl.parser);
    }
    if (status != XML_STATUS_OK) {
      fprintf(stderr, "FAIL: parse/resume failed with error %d: %s\n",
              (int)XML_GetErrorCode(rl.parser),
              XML_ErrorString(XML_GetErrorCode(rl.parser)));
      g_failures++;
      break;
    }
  }

  CHECK(sawSuspend, "parser never suspended in character-data handler");
  CHECK(rl.handlerError == XML_ERROR_NONE,
        "XML_StopParser failed inside character-data handler");
  /* No character may be published twice across the suspend/resume. */
  CHECK(strcmp(rl.text, "hello cross-chunk text") == 0,
        "character data duplicated or lost across suspend/resume");
  check_log(&rl, "S(r)C(h)C(ello)C( cro)C(ss-c)C(hunk)C( tex)C(t)E(r)",
            "cross-chunk event sequence mismatch");

  XML_ParserFree(rl.parser);
}

/* Resuming after termination (or without any suspension) is rejected
   with a status error. */
static void
test_resume_after_termination_rejected(void) {
  const char *const doc = "<doc/>";
  XML_Parser parser = XML_ParserCreate(NULL);
  enum XML_Status status;

  XML_SetUserData(parser, parser);
  XML_SetStartElementHandler(parser, aborting_start_element);

  status = XML_Parse(parser, doc, (int)strlen(doc), XML_TRUE);
  CHECK(status == XML_STATUS_ERROR, "terminated parse did not fail");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_ABORTED,
        "terminated parse did not fail with XML_ERROR_ABORTED");

  status = XML_ResumeParser(parser);
  CHECK(status == XML_STATUS_ERROR,
        "resume after termination was not rejected");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_NOT_SUSPENDED,
        "resume after termination gave wrong error code");

  XML_ParserFree(parser);

  /* A parser that was never suspended rejects resume as well. */
  parser = XML_ParserCreate(NULL);
  status = XML_ResumeParser(parser);
  CHECK(status == XML_STATUS_ERROR,
        "resume without prior suspend was not rejected");
  CHECK(XML_GetErrorCode(parser) == XML_ERROR_NOT_SUSPENDED,
        "resume without prior suspend gave wrong error code");

  XML_ParserFree(parser);
}

int
main(void) {
  test_suspend_at_second_start_event();
  test_resume_no_duplicate_chardata_across_chunks();
  test_resume_after_termination_rejected();

  if (g_failures == 0) {
    printf("PASS: all suspend/resume receipt tests passed\n");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "FAILED: %d check(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
