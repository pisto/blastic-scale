#pragma once

#ifndef BLASTIC_MONITOR_SPEED
#error Define the serial baud rate in BLASTIC_MONITOR_SPEED
#endif

#include <tuple>
#include <type_traits>

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
#include "ntp.h"

namespace blastic {

constexpr const char version[] = {BLASTIC_GIT_COMMIT " worktree " BLASTIC_GIT_WORKTREE_STATUS
                                                     " toolchain " BLASTIC_BUILD_SYSTEM};

extern uint32_t debug;

namespace eeprom {

enum class IOret { OK, ERROR, UPGRADED, NOT_FOUND, UNKONWN_VERSION };

template <uint32_t version> struct Config {

  struct Header {
    static constexpr const uint32_t expectedSignature = ((uint32_t('B') << 8 | 'L') << 8 | 'S') << 8 | 'C';
    uint32_t signature, Version;
  } header;

  // the conversion logic is implemented here, use `constexpr if` comparisons with versionFrom
  template <uint32_t versionFrom> Config &operator=(const Config<versionFrom> &o);

  template <uint32_t minVersion, typename enabledType>
  using fromVersion = util::fromVersion<version, minVersion, enabledType>;

  scale::Config scale;
  wifi::Layer3::Config wifi;
  blastic::Submitter::Config submit;
  buttons::Config buttons;
  fromVersion<1, SDCard::Config> sdcard;
  ntp::Config<version> ntp;

  std::tuple<IOret, uint32_t> load();
  IOret save() const;
  void sanitize();
  void defaults();
};

constexpr const uint32_t currentVersion = 3;

extern const uint32_t maxConfigLength;

} // namespace eeprom

using Config = eeprom::Config<eeprom::currentVersion>;

extern Config config;

// take these mutexes to access a global device
using MSerial = util::Mutexed<::Serial>;
using MWiFi = util::Mutexed<::WiFi>;

} // namespace blastic
