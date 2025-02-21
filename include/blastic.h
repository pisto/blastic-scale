#pragma once

#ifndef BLASTIC_MONITOR_SPEED
#error Define the serial baud rate in BLASTIC_MONITOR_SPEED
#endif

#include <tuple>

// environment
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include "Mutexed.h"

// classes / task functions for devices
#include "Scale.h"
#include "WifiConnection.h"
#include "Buttons.h"
#include "Submitter.h"
#include "SDCard.h"

namespace blastic {

constexpr const char version[] = {BLASTIC_GIT_COMMIT " worktree " BLASTIC_GIT_WORKTREE_STATUS
                                                     " toolchain " BLASTIC_BUILD_SYSTEM};

extern uint32_t debug;

namespace eeprom {

enum class IOret { OK, ERROR, UPGRADED, NOT_FOUND, UNKONWN_VERSION };

struct Header {
  static constexpr const uint32_t expectedSignature = ((uint32_t('B') << 8 | 'L') << 8 | 'S') << 8 | 'C',
                                  currentVersion = 1;
  uint32_t signature, version;
};

template <uint32_t version = Header::currentVersion> struct Config;
template <> struct Config<Header::currentVersion> {

  Header header;

  scale::Config scale;
  WifiConnection::Config wifi;
  blastic::Submitter::Config submit;
  buttons::Config buttons;
  SDCard::Config sdcard;

  std::tuple<IOret, uint32_t> load();
  IOret save() const;
  bool sanitize();
  void defaults();
};

extern const uint32_t maxConfigLength;

} // namespace eeprom

using Config = eeprom::Config<>;

extern Config config;

// take these mutexes to access a global device
using MSerial = util::Mutexed<::Serial>;
using MWiFi = util::Mutexed<::WiFi>;

} // namespace blastic
