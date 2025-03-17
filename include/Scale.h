#pragma once

#include <tuple>
#include <array>
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include "AnnotatedFloat.h"
#include "murmur32.h"

namespace blastic {

namespace scale {

// HX711Mode can be cast to an integer and used as index in arrays below
enum class HX711Mode : uint8_t { A128 = 0, B = 1, A64 = 2 };

struct Config {
  uint8_t dataPin, clockPin;
  HX711Mode mode;
  struct calibration {
    int32_t tareRead, calibrationRead;
    util::AnnotatedFloat calibrationWeight;
    operator bool() const { return !isnan(calibrationWeight); }
  };
  std::array<calibration, 3> calibrations;
  auto &getCalibration() { return calibrations[uint8_t(mode)]; }
  auto &getCalibration() const { return calibrations[uint8_t(mode)]; }
};

#define makeModeString(m) #m
static constexpr const char *modeStrings[]{makeModeString(A128), makeModeString(B), makeModeString(A64)};

constexpr const int32_t readErr = 0x800000;
const util::AnnotatedFloat weightCal = util::AnnotatedFloat("cal"), weightErr = util::AnnotatedFloat("err");
constexpr const uint32_t minReadDelayMillis = 1000 / 80; // max output rate is 80Hz

/*
  Read a raw value from HX711. Can run multiple measurements and get the median.

  This function switches on and back off the controller. The execution is
  also protected by a global mutex.
*/
int32_t raw(const Config &config, size_t medianWidth = 1, TickType_t timeout = portMAX_DELAY);

namespace debug {

// force a fake read
extern int32_t fake;

} // namespace debug

/*
  As above, but return a computed weight using calibration data.
*/
util::AnnotatedFloat weight(const Config &config, size_t medianWidth = 1, TickType_t timeout = portMAX_DELAY);

} // namespace scale

} // namespace blastic
