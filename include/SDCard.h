#pragma once

#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <SPI.h>
#include <SD.h>
#include "Mutexed.h"
#include "utils.h"

namespace blastic {

class SDCard : public util::Mutexed<::SD> {
  const bool initialized;

public:
  struct Config {
    uint8_t CSPin;
  };

  SDCard() : util::Mutexed<::SD>(), initialized((*this)->begin()) {}
  ~SDCard() {
    if (initialized) (*this)->end();
  }
  operator bool() const { return initialized; }
};

} // namespace blastic
