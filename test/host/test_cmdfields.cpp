/*
 * test_cmdfields.cpp - Task 6 (RVC + Microwave batch): the widened +MTCMD
 * dispatch that hands every endpoint type up to four tail payload fields at
 * once, HearthCmdFields, rather than the single optional field Task C7
 * (chime) added.
 *
 * Two probe subclasses:
 *
 * - FieldsProbe overrides the new hearthOnForwardedCommandFields() and
 *   records exactly what it was called with, so the parse can be asserted
 *   directly rather than inferred from a verdict.
 * - LegacyProbe overrides only the pre-existing hearthOnForwardedCommand()
 *   (the four-argument form) and never even hears about
 *   hearthOnForwardedCommandFields(): it exists to prove the base class's
 *   default Fields implementation still delegates down to the legacy
 *   virtual byte-identically, which is the whole backward-compatibility
 *   contract this task exists to keep. Every one of the other 41 test
 *   binaries in this suite is really the same proof, run against a real
 *   endpoint type instead of a probe.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

class FieldsProbe : public MatterEndPoint {
public:
  int calls = 0;
  uint32_t seenCluster = 0;
  uint32_t seenCommand = 0;
  HearthCmdFields seenFields = {};
  bool verdict = true;

  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    return true;
  }
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override {
    calls++;
    seenCluster = cluster_id;
    seenCommand = command_id;
    seenFields = fields;
    return verdict;
  }
};

class LegacyProbe : public MatterEndPoint {
public:
  int calls = 0;
  bool seenHasPayload = false;
  uint32_t seenPayload = 0;
  bool verdict = true;

  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    return true;
  }
  bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) override {
    calls++;
    seenHasPayload = hasPayload;
    seenPayload = payload;
    (void)cluster_id;
    (void)command_id;
    return verdict;
  }
};

/* Step 1's first case: a four-field line, every field present. */
static void test_four_fields_all_present(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  FieldsProbe ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(2);
  Hearth.begin(s);

  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,2,95,0,1,30,80,1");
  Hearth.poll();

  check("dispatched exactly once", ep.calls == 1);
  check("cluster id parsed", ep.seenCluster == 95);
  check("command id parsed", ep.seenCommand == 0);
  check("count is 4", ep.seenFields.count == 4);
  check("all four fields present", ep.seenFields.present[0] && ep.seenFields.present[1] &&
                                        ep.seenFields.present[2] && ep.seenFields.present[3]);
  check("values are 1,30,80,1", ep.seenFields.value[0] == 1 && ep.seenFields.value[1] == 30 &&
                                     ep.seenFields.value[2] == 80 && ep.seenFields.value[3] == 1);
  check("an allowing verdict answers AT+MTCMDRESP=9,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Step 1's second case: a sparse line, empty fields between commas. */
static void test_sparse_fields_present_flags(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  FieldsProbe ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(2);
  Hearth.begin(s);

  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,2,95,0,,30,,1");
  Hearth.poll();

  check("dispatched exactly once", ep.calls == 1);
  check("count is still 4 (position, not presence, is counted)", ep.seenFields.count == 4);
  check("present flags are false,true,false,true", !ep.seenFields.present[0] && ep.seenFields.present[1] &&
                                                        !ep.seenFields.present[2] && ep.seenFields.present[3]);
  check("the present values are 30 and 1", ep.seenFields.value[1] == 30 && ep.seenFields.value[3] == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Step 1's third case: a legacy no-payload line against a subclass that only
 * overrides the old four-argument virtual. Old behaviour: hasPayload false,
 * payload 0. */
static void test_legacy_no_payload_unchanged(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  LegacyProbe ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(2);
  Hearth.begin(s);

  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,2,96,0");
  Hearth.poll();

  check("legacy virtual is still reached", ep.calls == 1);
  check("hasPayload is false", !ep.seenHasPayload);
  check("payload is 0", ep.seenPayload == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Step 1's fourth case: a legacy single-payload line (the C7/chime shape)
 * against the same legacy-only subclass. Old behaviour: hasPayload true,
 * payload carries the one field. */
static void test_legacy_single_payload_unchanged(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  LegacyProbe ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(2);
  Hearth.begin(s);

  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,2,1286,0,7");
  Hearth.poll();

  check("legacy virtual is still reached", ep.calls == 1);
  check("hasPayload is true", ep.seenHasPayload);
  check("payload is 7", ep.seenPayload == 7);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A denying verdict from the Fields override must still enqueue exactly the
 * way the legacy path always did, since hearthDispatchCmd's verdict handling
 * did not change, only how it gets the verdict. */
static void test_fields_deny_sends_verdict_0(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  FieldsProbe ep;
  ep.verdict = false;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(2);
  Hearth.begin(s);

  s.expect("AT+MTCMDRESP=11,0", "OK\r\n");
  s.injectURC("+MTCMD:11,2,95,0,1,30,80,1");
  Hearth.poll();

  check("a denying Fields override answers AT+MTCMDRESP=11,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Seq 0 is notify-only: dispatch still runs (the Fields override still
 * fires) but no AT+MTCMDRESP is ever sent for it, exactly as the legacy
 * path already guaranteed (Hearth.cpp's own comment on hearthDispatchCmd).
 */
static void test_seq_zero_notify_only_still_dispatches_fields(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  FieldsProbe ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(2);
  Hearth.begin(s);

  s.injectURC("+MTCMD:0,2,95,0,1,30,80,1");
  Hearth.poll();

  check("dispatch still ran for seq 0", ep.calls == 1);
  check("no AT+MTCMDRESP is issued for a notify-only seq", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== HearthCmdFields / multi-field +MTCMD dispatch tests =====\n");
  test_four_fields_all_present();
  test_sparse_fields_present_flags();
  test_legacy_no_payload_unchanged();
  test_legacy_single_payload_unchanged();
  test_fields_deny_sends_verdict_0();
  test_seq_zero_notify_only_still_dispatches_fields();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
