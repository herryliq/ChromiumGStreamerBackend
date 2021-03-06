// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/errors.h"

#include "base/logging.h"
#include "remoting/protocol/name_value_map.h"

namespace remoting {
namespace protocol {

namespace {

const NameMapElement<ErrorCode> kErrorCodeNames[] = {
  { OK, "OK" },
  { PEER_IS_OFFLINE, "PEER_IS_OFFLINE" },
  { SESSION_REJECTED, "SESSION_REJECTED" },
  { INCOMPATIBLE_PROTOCOL, "INCOMPATIBLE_PROTOCOL" },
  { AUTHENTICATION_FAILED, "AUTHENTICATION_FAILED" },
  { INVALID_ACCOUNT, "INVALID_ACCOUNT" },
  { CHANNEL_CONNECTION_ERROR, "CHANNEL_CONNECTION_ERROR" },
  { SIGNALING_ERROR, "SIGNALING_ERROR" },
  { SIGNALING_TIMEOUT, "SIGNALING_TIMEOUT" },
  { HOST_OVERLOAD, "HOST_OVERLOAD" },
  { MAX_SESSION_LENGTH, "MAX_SESSION_LENGTH" },
  { HOST_CONFIGURATION_ERROR, "HOST_CONFIGURATION_ERROR" },
  { UNKNOWN_ERROR, "UNKNOWN_ERROR" },
};

}  // namespace

const char* ErrorCodeToString(ErrorCode error) {
  return ValueToName(kErrorCodeNames, error);
}

bool ParseErrorCode(const std::string& name, ErrorCode* result) {
  return NameToValue(kErrorCodeNames, name, result);
}

}  // namespace protocol
}  // namespace remoting
