// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluez/bluetooth_gatt_service_bluez.h"

#include "base/logging.h"
#include "device/bluetooth/bluetooth_gatt_service.h"
#include "device/bluetooth/bluez/bluetooth_adapter_bluez.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace bluez {

BluetoothGattServiceBlueZ::BluetoothGattServiceBlueZ(
    BluetoothAdapterBlueZ* adapter,
    dbus::ObjectPath object_path)
    : adapter_(adapter), object_path_(object_path) {
  DCHECK(adapter_);
}

BluetoothGattServiceBlueZ::~BluetoothGattServiceBlueZ() {}

std::string BluetoothGattServiceBlueZ::GetIdentifier() const {
  return object_path_.value();
}

// static
device::BluetoothGattService::GattErrorCode
BluetoothGattServiceBlueZ::DBusErrorToServiceError(std::string error_name) {
  device::BluetoothGattService::GattErrorCode code = GATT_ERROR_UNKNOWN;
  if (error_name == bluetooth_gatt_service::kErrorFailed) {
    code = GATT_ERROR_FAILED;
  } else if (error_name == bluetooth_gatt_service::kErrorInProgress) {
    code = GATT_ERROR_IN_PROGRESS;
  } else if (error_name == bluetooth_gatt_service::kErrorInvalidValueLength) {
    code = GATT_ERROR_INVALID_LENGTH;
  } else if (error_name == bluetooth_gatt_service::kErrorReadNotPermitted ||
             error_name == bluetooth_gatt_service::kErrorWriteNotPermitted) {
    code = GATT_ERROR_NOT_PERMITTED;
  } else if (error_name == bluetooth_gatt_service::kErrorNotAuthorized) {
    code = GATT_ERROR_NOT_AUTHORIZED;
  } else if (error_name == bluetooth_gatt_service::kErrorNotPaired) {
    code = GATT_ERROR_NOT_PAIRED;
  } else if (error_name == bluetooth_gatt_service::kErrorNotSupported) {
    code = GATT_ERROR_NOT_SUPPORTED;
  }
  return code;
}

BluetoothAdapterBlueZ* BluetoothGattServiceBlueZ::GetAdapter() const {
  return adapter_;
}

}  // namespace bluez
