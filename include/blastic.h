#pragma once

#ifndef BLASTIC_MONITOR_SPEED
#error Define the serial baud rate in BLASTIC_MONITOR_SPEED
#endif

// environment
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include "Mutexed.h"

// classes / task functions for devices
#include "Scale.h"
#include "WifiConnection.h"
#include "Buttons.h"
#include "Submitter.h"

namespace blastic {

constexpr const char version[] = {BLASTIC_GIT_COMMIT " worktree " BLASTIC_GIT_WORKTREE_STATUS};

extern uint32_t debug;

namespace eeprom {

enum class IOret { OK, ERROR, UPGRADED, NOT_FOUND, UNKONWN_VERSION };

struct Header {
  static constexpr const uint32_t expectedSignature = ((uint32_t('B') << 8 | 'L') << 8 | 'S') << 8 | 'C', currentVersion = 0;
  uint32_t signature, version;
};

template <uint32_t version = Header::currentVersion> struct Config;
template <> struct Config<Header::currentVersion> {

  Header header;

  scale::Config scale;
  WifiConnection::Config wifi;
  blastic::Submitter::Config submit;
  buttons::Config buttons;

  void sanitize();
  static const Config defaults;

  IOret load();
  IOret save() const;
};

extern const uint32_t maxConfigLength;

} // namespace eeprom

using Config = eeprom::Config<>;

extern Config config;

// take these mutexes to access a global device
using MSerial = util::Mutexed<::Serial>;
using MWiFi = util::Mutexed<::WiFi>;

} // namespace blastic
