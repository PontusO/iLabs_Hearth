/*
 * MatterCookSurface.cpp - implementation. See the header for the design
 * rationale (subclassing the cabinet for the temperature machinery, the
 * owned-only begin gates, the OnOff OffOnly surface and why the inherited
 * cluster-82 paths are dead here), MatterCooktop.h for the owner's side of
 * the contract, and MatterOvenCavity.cpp for the typed-owned-child pattern
 * this mirrors.
 */
#include "MatterEndpoints/MatterCookSurface.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* chip::app::Clusters::OnOff::Id and OnOff::Attributes::OnOff::Id, the
 * same IDs every OnOff-cluster class in this library uses (see
 * MatterCooktop.cpp). Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
}  // namespace

MatterCookSurface::MatterCookSurface() {}

MatterCookSurface::~MatterCookSurface() {
  /* The base class chain (cabinet end(), MatterEndPoint's undeclare) does
   * the real teardown; only this class's own additions reset here. */
  surfaceOnOff = false;
}

/*
 * Both begin() overloads: OWNED ONLY. A surface no cooktop owns refuses
 * before the base class could ever reach its standalone hearthDeclare()
 * path: 0x0077 REQUIRES a parent on the wire (the header comment), so a
 * standalone declaration could never be applied anyway; refusing here
 * surfaces the error at the begin() call instead of deep inside
 * Matter.begin()'s apply. An owned surface takes the base's owned branch,
 * which validates the flavour against the declared variant and caches
 * without declaring (the parent's declaration is authoritative, parent
 * index and all). On success the surface's own OnOff cache resets
 * alongside the base's state: cache and device both boot Off, which is
 * what makes setOnOff()'s skip-if-equal sound.
 */
bool MatterCookSurface::begin(double tempSetpoint, double minTemperature, double maxTemperature, double step) {
  if (!hearthOwnedByFridge) {
    return false;
  }
  if (!MatterTemperatureControlledCabinet::begin(tempSetpoint, minTemperature, maxTemperature, step)) {
    return false;
  }
  surfaceOnOff = false;
  return true;
}

bool MatterCookSurface::begin(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel) {
  if (!hearthOwnedByFridge) {
    return false;
  }
  if (!MatterTemperatureControlledCabinet::begin(supportedLevels, levelCount, selectedLevel)) {
    return false;
  }
  surfaceOnOff = false;
  return true;
}

void MatterCookSurface::onOffChange(std::function<void(bool)> cb) {
  _onOffChangeCB = cb;
}

/*
 * The host's own switch, both directions (AT_MT_SPEC.md S3.9's 0x0077
 * note: remote ON does not exist on the OffOnly cluster, so turning a
 * burner on is always this write). updateAttributeVal (mode 1, reported)
 * on purpose: the burner state is a genuine device-side change a
 * subscribed controller must observe, not a reflection of something the
 * fabric already knows (which is what setAttributeVal's silent mode 0
 * exists for). Skip-if-equal is sound because cache and device both boot
 * Off (see begin()); cache commit only on a successful write.
 */
bool MatterCookSurface::setOnOff(bool state) {
  if (!started) {
    return false;
  }
  if (surfaceOnOff == state) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(state);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;  /* cache untouched on a failed write */
  }
  surfaceOnOff = state;
  return true;
}

bool MatterCookSurface::getOnOff() {
  return surfaceOnOff;
}

/*
 * Hides the base class's fridge-cabinet (0x52) version: no mode cluster of
 * any kind exists on a surface endpoint, so this refuses without wire
 * traffic even though the surface IS owned (the base version's gate is
 * ownership, which would wrongly let it through here). Refusing keeps the
 * base reconcile hook's cluster-82 resend structurally dead too: nothing
 * can ever populate the inherited arrays through this class.
 */
bool MatterCookSurface::setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  (void)modes;
  (void)tags;
  (void)labels;
  (void)count;
  return false;
}

/*
 * The surface adjudicates nothing: its OnOff cluster raises attribute URCs
 * (the firmware's own server answers the Off command), never +MTCMD
 * forwards, and no other command-bearing cluster exists on the endpoint.
 * Defer everything to MatterEndPoint's fail-closed default DIRECTLY,
 * deliberately skipping the cabinet base class's owned-cabinet cluster-82
 * adjudication: a spurious cluster-82 forward on a surface must be denied
 * without consulting the inherited callback (the MatterOvenCavity
 * precedent; see both header comments).
 */
bool MatterCookSurface::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}

/*
 * OnOff URCs land here: a controller's Off (the only remote-deliverable
 * value, per OffOnly) and the firmware's echo of this host's own
 * setOnOff() writes both arrive as +MTATTR on cluster 6. Cache follows the
 * wire unconditionally (the wire is the device's truth), then the callback
 * fires, deliberately unfiltered (see the header). No write-back: the
 * change already lives on the fabric, and echoing it through
 * updateAttributeVal would loop (the house rule every sibling class
 * documents). Everything else, i.e. the cluster-86 temperature machinery,
 * defers to the cabinet base class.
 */
bool MatterCookSurface::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id == getEndPointId() && cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    surfaceOnOff = val->val.b;
    if (_onOffChangeCB != nullptr) {
      _onOffChangeCB(surfaceOnOff);
    }
    return true;
  }
  return MatterTemperatureControlledCabinet::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
}

esp_matter_val_type_t MatterCookSurface::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterTemperatureControlledCabinet::hearthAttrTypeFor(cluster_id, attribute_id);
}
