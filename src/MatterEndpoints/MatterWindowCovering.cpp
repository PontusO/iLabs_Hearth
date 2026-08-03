/*
 * MatterWindowCovering.cpp - implementation. See the header for the three
 * documented deviations from a literal transcription of upstream's .cpp
 * (no wire traffic from begin(), cache-only getters, and the two small
 * caches upstream does not need but this stack does).
 */
#include "MatterEndpoints/MatterWindowCovering.h"

namespace {
/* window_covering (esp_matter_endpoint.h's
 * ESP_MATTER_WINDOW_COVERING_DEVICE_TYPE_ID),
 * chip::app::Clusters::WindowCovering::Id, and
 * WindowCovering::Attributes::{Type,ConfigStatus,OperationalStatus,
 * TargetPositionLiftPercent100ths,TargetPositionTiltPercent100ths,
 * CurrentPositionLiftPercent100ths,CurrentPositionTiltPercent100ths}::Id.
 * Given as plain integers: there is no connectedhomeip header on a host
 * build to pull the named constants from. */
const uint32_t kWindowCoveringDeviceType = 0x0202;
const uint32_t kWindowCoveringClusterId = 0x0102;
const uint32_t kTypeAttributeId = 0x0000;
const uint32_t kConfigStatusAttributeId = 0x0007;
const uint32_t kOperationalStatusAttributeId = 0x000A;
const uint32_t kTargetPositionLiftPercent100thsAttributeId = 0x000B;
const uint32_t kTargetPositionTiltPercent100thsAttributeId = 0x000C;
const uint32_t kCurrentPositionLiftPercent100thsAttributeId = 0x000E;
const uint32_t kCurrentPositionTiltPercent100thsAttributeId = 0x000F;
}  // namespace

MatterWindowCovering::MatterWindowCovering() {}

MatterWindowCovering::~MatterWindowCovering() {
  end();
}

bool MatterWindowCovering::begin(uint8_t liftPercent, uint8_t tiltPercent, WindowCoveringType_t _coveringType) {
  if (!hearthDeclare(this, kWindowCoveringDeviceType)) {
    return false;
  }
  coveringType = (_coveringType == 0) ? ROLLERSHADE : _coveringType;

  currentLiftPercent = liftPercent;
  currentLiftPercent100ths = (uint16_t)liftPercent * 100;
  targetLiftPercent100ths = currentLiftPercent100ths;
  currentLiftPosition = 0;

  currentTiltPercent = tiltPercent;
  currentTiltPercent100ths = (uint16_t)tiltPercent * 100;
  targetTiltPercent100ths = currentTiltPercent100ths;
  currentTiltPosition = 0;

  operationalStatus = 0;
  started = true;
  return true;
}

void MatterWindowCovering::end() {
  started = false;
}

bool MatterWindowCovering::setLiftPercentage(uint8_t liftPercent) {
  if (!started) {
    return false;
  }
  if (liftPercent > 100) {
    return false;
  }
  if (currentLiftPercent == liftPercent) {
    return true;
  }
  uint16_t liftPercent100ths = (uint16_t)liftPercent * 100;
  esp_matter_attr_val_t val = esp_matter_uint16(liftPercent100ths);
  if (!updateAttributeVal(kWindowCoveringClusterId, kCurrentPositionLiftPercent100thsAttributeId, &val)) {
    return false;
  }
  currentLiftPercent = liftPercent;
  currentLiftPercent100ths = liftPercent100ths;
  return true;
}

bool MatterWindowCovering::setTiltPercentage(uint8_t tiltPercent) {
  if (!started) {
    return false;
  }
  if (tiltPercent > 100) {
    return false;
  }
  if (currentTiltPercent == tiltPercent) {
    return true;
  }
  uint16_t tiltPercent100ths = (uint16_t)tiltPercent * 100;
  esp_matter_attr_val_t val = esp_matter_uint16(tiltPercent100ths);
  if (!updateAttributeVal(kWindowCoveringClusterId, kCurrentPositionTiltPercent100thsAttributeId, &val)) {
    return false;
  }
  currentTiltPercent = tiltPercent;
  currentTiltPercent100ths = tiltPercent100ths;
  return true;
}

bool MatterWindowCovering::setTargetLiftPercent100ths(uint16_t liftPercent100ths) {
  if (!started) {
    return false;
  }
  if (liftPercent100ths > 10000) {
    return false;
  }
  if (targetLiftPercent100ths == liftPercent100ths) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(liftPercent100ths);
  if (!updateAttributeVal(kWindowCoveringClusterId, kTargetPositionLiftPercent100thsAttributeId, &val)) {
    return false;
  }
  targetLiftPercent100ths = liftPercent100ths;
  return true;
}

bool MatterWindowCovering::setTargetTiltPercent100ths(uint16_t tiltPercent100ths) {
  if (!started) {
    return false;
  }
  if (tiltPercent100ths > 10000) {
    return false;
  }
  if (targetTiltPercent100ths == tiltPercent100ths) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(tiltPercent100ths);
  if (!updateAttributeVal(kWindowCoveringClusterId, kTargetPositionTiltPercent100thsAttributeId, &val)) {
    return false;
  }
  targetTiltPercent100ths = tiltPercent100ths;
  return true;
}

bool MatterWindowCovering::setCoveringType(WindowCoveringType_t newType) {
  if (!started) {
    return false;
  }
  if (coveringType == newType) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_enum8((uint8_t)newType);
  if (!updateAttributeVal(kWindowCoveringClusterId, kTypeAttributeId, &val)) {
    return false;
  }
  coveringType = newType;
  return true;
}

bool MatterWindowCovering::setOperationalStatus(uint8_t newStatus) {
  if (!started) {
    return false;
  }
  if (operationalStatus == newStatus) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bitmap8(newStatus);
  if (!updateAttributeVal(kWindowCoveringClusterId, kOperationalStatusAttributeId, &val)) {
    return false;
  }
  operationalStatus = newStatus;
  return true;
}

/*
 * Mirrors MatterWindowCovering.cpp's setOperationalState() exactly, reading
 * the current bitmap from the operationalStatus cache in place of upstream's
 * getAttributeVal() call (see the header's point 3). GLOBAL cannot be set
 * directly: it is recomputed from LIFT (priority) and TILT below, matching
 * upstream's own rule.
 */
bool MatterWindowCovering::setOperationalState(OperationalStatusField_t field, OperationalState_t state) {
  if (!started) {
    return false;
  }
  if (field == GLOBAL) {
    return false;
  }
  if (field != LIFT && field != TILT) {
    return false;
  }

  uint8_t currentStatus = operationalStatus;
  uint8_t fieldMask = (uint8_t)field;
  uint8_t fieldShift = (field == LIFT) ? 2 : 4;

  uint8_t currentFieldState = (currentStatus & fieldMask) >> fieldShift;
  if (currentFieldState == (uint8_t)state) {
    return true;
  }

  currentStatus = (currentStatus & ~fieldMask) | (((uint8_t)state << fieldShift) & fieldMask);

  uint8_t liftState = (currentStatus & LIFT) >> 2;
  uint8_t tiltState = (currentStatus & TILT) >> 4;
  uint8_t globalState = (liftState != STALL) ? liftState : tiltState;
  currentStatus = (currentStatus & ~GLOBAL) | (globalState << 0);

  esp_matter_attr_val_t val = esp_matter_bitmap8(currentStatus);
  if (!updateAttributeVal(kWindowCoveringClusterId, kOperationalStatusAttributeId, &val)) {
    return false;
  }
  operationalStatus = currentStatus;
  return true;
}

MatterWindowCovering::OperationalState_t MatterWindowCovering::getOperationalState(OperationalStatusField_t field) {
  uint8_t fieldShift;
  if (field == GLOBAL) {
    fieldShift = 0;
  } else if (field == LIFT) {
    fieldShift = 2;
  } else if (field == TILT) {
    fieldShift = 4;
  } else {
    return STALL;
  }
  uint8_t fieldState = (operationalStatus & (uint8_t)field) >> fieldShift;
  return (OperationalState_t)fieldState;
}

/*
 * TargetPositionLiftPercent100ths carries the UpOrOpen/DownOrClose/
 * StopMotion command detection upstream's own attributeChangeCB implements:
 * target==0 is UpOrOpen, target==10000 is DownOrClose, target==current
 * (away from either limit) is StopMotion, and onGoToLiftPercentage() fires
 * unconditionally alongside whichever of those matched. "current" here is
 * the currentLiftPercent100ths cache (point 3 in the header), in place of
 * upstream's getAttributeVal() fetch.
 */
bool MatterWindowCovering::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kWindowCoveringClusterId) {
    return ret;
  }

  if (attribute_id == kCurrentPositionLiftPercent100thsAttributeId) {
    uint16_t liftPercent100ths = val->val.u16;
    uint8_t liftPercent = (uint8_t)(liftPercent100ths / 100);
    if (currentLiftPercent != liftPercent) {
      if (_onChangeCB != NULL) {
        ret &= _onChangeCB(liftPercent, currentTiltPercent);
      }
    }
    if (ret) {
      currentLiftPercent = liftPercent;
      currentLiftPercent100ths = liftPercent100ths;
    }
  } else if (attribute_id == kCurrentPositionTiltPercent100thsAttributeId) {
    uint16_t tiltPercent100ths = val->val.u16;
    uint8_t tiltPercent = (uint8_t)(tiltPercent100ths / 100);
    if (currentTiltPercent != tiltPercent) {
      if (_onChangeCB != NULL) {
        ret &= _onChangeCB(currentLiftPercent, tiltPercent);
      }
    }
    if (ret) {
      currentTiltPercent = tiltPercent;
      currentTiltPercent100ths = tiltPercent100ths;
    }
  } else if (attribute_id == kTargetPositionLiftPercent100thsAttributeId) {
    uint16_t targetLift100ths = val->val.u16;
    uint8_t targetLiftPercent = (uint8_t)(targetLift100ths / 100);

    if (targetLift100ths == 0) {
      if (_onOpenCB != NULL) {
        ret &= _onOpenCB();
      }
    } else if (targetLift100ths == 10000) {
      if (_onCloseCB != NULL) {
        ret &= _onCloseCB();
      }
    } else if (targetLift100ths == currentLiftPercent100ths && currentLiftPercent100ths != 0 && currentLiftPercent100ths != 10000) {
      if (_onStopCB != NULL) {
        ret &= _onStopCB();
      }
    }

    if (_onGoToLiftPercentageCB != NULL) {
      ret &= _onGoToLiftPercentageCB(targetLiftPercent);
    }
    if (ret) {
      targetLiftPercent100ths = targetLift100ths;
    }
  } else if (attribute_id == kTargetPositionTiltPercent100thsAttributeId) {
    uint16_t targetTilt100ths = val->val.u16;
    uint8_t targetTiltPercent = (uint8_t)(targetTilt100ths / 100);
    if (_onGoToTiltPercentageCB != NULL) {
      ret &= _onGoToTiltPercentageCB(targetTiltPercent);
    }
    if (ret) {
      targetTiltPercent100ths = targetTilt100ths;
    }
  } else if (attribute_id == kTypeAttributeId) {
    coveringType = (WindowCoveringType_t)val->val.u8;
  } else if (attribute_id == kOperationalStatusAttributeId) {
    /* Cached here, unlike upstream (see header point 3): our
     * getOperationalStatus()/getOperationalState() read the cache, not a
     * live attribute store. */
    operationalStatus = val->val.u8;
  } else if (attribute_id == kConfigStatusAttributeId) {
    /* No cache, no callback: matches upstream, which only logs this one. */
  }
  return ret;
}

esp_matter_val_type_t MatterWindowCovering::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kWindowCoveringClusterId) {
    if (attribute_id == kTypeAttributeId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kConfigStatusAttributeId || attribute_id == kOperationalStatusAttributeId) {
      return ESP_MATTER_VAL_TYPE_BITMAP8;
    }
    if (attribute_id == kTargetPositionLiftPercent100thsAttributeId || attribute_id == kTargetPositionTiltPercent100thsAttributeId
        || attribute_id == kCurrentPositionLiftPercent100thsAttributeId || attribute_id == kCurrentPositionTiltPercent100thsAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT16;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}
