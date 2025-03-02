#pragma once

#include "utils.h"

namespace ntp {

template <size_t version> struct Config {

  template <uint32_t minVersion, typename enabledType>
  using fromVersion = util::fromVersion<version, minVersion, enabledType>;

  fromVersion<2, util::StringBuffer<128>> hostname;
  fromVersion<3, uint32_t> refresh;

};

int unixTime();
void startSync(bool force = false);

} // namespace ntp
