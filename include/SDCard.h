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

  SDCard(uint8_t CSPin) : util::Mutexed<::SD>(), initialized((*this)->begin(CSPin)) {}
  ~SDCard() {
    if (initialized) (*this)->end();
  }
  operator bool() const { return initialized; }
};

} // namespace blastic

/*
  Annoyingly, the SD.begin() API cannot return failure reasons unless a private member is accessed.
*/

ClassPrivateMemberAccessor(SDClass, Sd2Card, card);
