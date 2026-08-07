/*
 * MatterChime.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the "no AT+MTATTR
 * path at all" fact that makes attributeChangeCB() a documented no-op, and
 * the InstalledChimeSounds-vs-SelectedChime/Enabled persistence split that
 * decides which state this class pushes at reconcile.
 */
#include "MatterEndpoints/MatterChime.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* chime (ESP_MATTER_CHIME_DEVICE_TYPE_ID), Chime::Id, and
 * Chime::Commands::PlayChimeSound::Id. See MatterChime.h's header comment
 * for the quoted lines from the pinned esp-matter checkout's generated
 * headers. Given as plain integers: there is no connectedhomeip header on
 * a host build to pull the named constants from. */
const uint32_t kChimeDeviceType = 0x0146;
const uint32_t kChimeClusterId = 0x0556;  // 1366 decimal
const uint32_t kPlayChimeSoundCommandId = 0x0000;

/* AT+MTCHIME=<ep>,<what>,<value> (AT_MT_SPEC.md S3.24): <what> selects
 * which of the cluster's two plain attributes the value sets. */
const uint8_t kWhatSelectedChime = 0;
const uint8_t kWhatEnabled = 1;
}  // namespace

MatterChime::MatterChime() {}

MatterChime::~MatterChime() {
  end();
}

bool MatterChime::begin() {
  if (!hearthDeclare(this, kChimeDeviceType)) {
    return false;
  }
  selectedChime = 0;
  enabled = false;
  installedCount = 0;
  started = true;
  return true;
}

void MatterChime::end() {
  started = false;
  installedCount = 0;
}

void MatterChime::onPlayChime(std::function<bool(uint8_t chimeID)> cb) {
  _onPlayChimeCB = cb;
}

/*
 * Builds and sends "AT+MTCHIMESOUNDS=<ep>,<id1>,"<name1>",..." (AT_MT_SPEC.md
 * S3.23) for exactly the pairs given. Wire-only: the caller decides
 * whether/what to commit to the cache afterwards.
 */
bool MatterChime::hearthSendInstalledSounds(const uint8_t *ids, const char *const *names, uint8_t count) {
  char cmd[400];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTCHIMESOUNDS=%u", (unsigned)getEndPointId());
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,\"%s\"", (unsigned)ids[i], names[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Host-side grammar enforcement (S3.23, see the header comment): count
 * bounds, id uniqueness within THIS call's list, and per-name
 * length/printability/quote-exclusion -- the identical shape
 * MatterModeSelect::setSupportedModes() enforces for the identically-shaped
 * AT+MTMODES grammar S3.23 itself is defined in terms of. Every violation
 * reports Hearth.hearthSetError(1), the wire's own "grammar violation"
 * code, without ever reaching the wire.
 */
bool MatterChime::setInstalledChimeSounds(const uint8_t *ids, const char *const *names, uint8_t count) {
  if (!started) {
    return false;
  }
  if (ids == nullptr || names == nullptr || count == 0 || count > kMaxSounds) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
      if (ids[i] == ids[j]) {
        Hearth.hearthSetError(1);
        return false;
      }
    }
    if (names[i] == nullptr) {
      Hearth.hearthSetError(1);
      return false;
    }
    size_t len = strlen(names[i]);
    if (len == 0 || len > kMaxNameLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* Printable ASCII, no '"'; a comma is deliberately allowed (S3.23:
     * legal inside a quoted name). Same grammar MatterModeSelect enforces. */
    for (const char *p = names[i]; *p != '\0'; p++) {
      unsigned char ch = (unsigned char)*p;
      if (ch < 0x20 || ch > 0x7E || ch == '"') {
        Hearth.hearthSetError(1);
        return false;
      }
    }
  }
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  if (!hearthSendInstalledSounds(ids, names, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(installedIdsArray, ids, count * sizeof(uint8_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(installedNames[i], names[i], kMaxNameLen);
    installedNames[i][kMaxNameLen] = '\0';
  }
  installedCount = count;
  return true;
}

/*
 * AT+MTCHIME=<ep>,<what>,<value> (AT_MT_SPEC.md S3.24). getEndPointId() ==
 * 0 is checked directly here, not through hearthEndPointAddressable():
 * that guard is private to MatterEndPoint and used internally by
 * setAttributeVal()/updateAttributeVal(), which this cluster has no path
 * to at all (see the header comment) -- the same pattern
 * MatterDoorLock::hearthSendLockState() establishes for a custom wire verb.
 */
bool MatterChime::hearthSendChimeField(uint8_t what, uint8_t value) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTCHIME=%u,%u,%u", (unsigned)getEndPointId(), (unsigned)what, (unsigned)value);
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterChime::setSelectedChime(uint8_t id) {
  if (!started) {
    return false;
  }
  if (selectedChime == id) {
    return true;
  }
  if (!hearthSendChimeField(kWhatSelectedChime, id)) {
    return false;  // cache untouched on a failed write
  }
  selectedChime = id;
  return true;
}

uint8_t MatterChime::getSelectedChime() {
  return selectedChime;
}

bool MatterChime::setEnabled(bool on) {
  if (!started) {
    return false;
  }
  if (enabled == on) {
    return true;
  }
  if (!hearthSendChimeField(kWhatEnabled, on ? 1 : 0)) {
    return false;  // cache untouched on a failed write
  }
  enabled = on;
  return true;
}

bool MatterChime::getEnabled() {
  return enabled;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first: resends the cached InstalledChimeSounds list, which the
 * firmware does not persist across a reboot (S3.23). SelectedChime/Enabled
 * need no push here: S3.24 states they persist on the firmware side across
 * AT+MTRESET, so there is no cache/device mismatch for a reconcile to fix
 * the way MatterDoorLock's LockState push exists to fix. A no-op until the
 * sketch has called setInstalledChimeSounds() at least once.
 */
void MatterChime::hearthOnReconciled() {
  if (!started || installedCount == 0) {
    return;
  }
  const char *namePtrs[kMaxSounds];
  for (uint8_t i = 0; i < installedCount; i++) {
    namePtrs[i] = installedNames[i];
  }
  hearthSendInstalledSounds(installedIdsArray, namePtrs, installedCount);
}

/*
 * A documented no-op: see the header comment. No attribute on this cluster
 * has an AT+MTATTR path (InstalledChimeSounds/SelectedChime/Enabled are
 * all set through their own dedicated commands instead), so
 * hearthDispatchAttr() (Hearth.cpp) never has a (cluster, attribute) pair
 * from THIS cluster to route here in the first place. Present only because
 * MatterEndPoint declares this pure virtual -- the same shape
 * MatterGenericSwitch's own header comment establishes for a class with no
 * AT+MTATTR-reachable attribute at all.
 */
bool MatterChime::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

/*
 * AT_MT_SPEC.md S3.17/S3.24: the firmware forwards a controller-invoked
 * PlayChimeSound here for a verdict, carrying the requested chimeID in the
 * reserved fifth `+MTCMD` field. Fails closed (false, i.e. deny) for the
 * wrong cluster, an unstarted endpoint, an unrecognised command id, or no
 * callback registered. Unlike MatterWaterValve, this verdict IS the real
 * wire response the controller sees (header comment): a deny genuinely
 * fails the command. hasPayload is not checked before reading payload: the
 * SDK never raises PlayChimeSound's +MTCMD without the chimeID field (it
 * is the entire reason the command needs adjudication), so an absent
 * payload here would itself be the malformed case the wire-level parser
 * already declines to hand off as anything meaningful; the callback simply
 * sees chimeID 0 in that structurally-unexpected case, matching this
 * class's usual "fail toward zero, not toward a crash" discipline.
 */
bool MatterChime::hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) {
  if (!started || cluster_id != kChimeClusterId || command_id != kPlayChimeSoundCommandId) {
    return false;
  }
  uint8_t chimeID = hasPayload ? (uint8_t)payload : 0;
  return _onPlayChimeCB ? _onPlayChimeCB(chimeID) : false;
}
