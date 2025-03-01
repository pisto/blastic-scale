#pragma once

#include "utils.h"

namespace ntp {

struct Config {
  util::StringBuffer<128> hostname;
};

int unixTime();
void startSync(bool force = false);

} // namespace ntp
