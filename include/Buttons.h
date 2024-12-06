#pragma once

#include <array>
#include <functional>
#include "StaticTask.h"
#include <R4_Touch.h>

namespace blastic {

/*
  DebouncedTouchSensor has a running counter of detected high/low states, and emits a rising/falling edge signal when
  more than 75% of the last detected values are different than the current button state. As an example, when the button
  is not touched, you need 32 * 75% = 24 measurements in the touched state to effectively trigger the rising edge
  signal.
*/

class DebouncedTouchSensor : public TouchSensor {
public:
  struct Config {
    uint8_t pin;
    uint16_t threshold;
    ctsu_pin_settings_t settings;
  };

  using TouchSensor::TouchSensor;

  bool updateRead();
  operator bool() const { return triggered; }

  static void measurementCallback();

protected:
  volatile uint32_t lastMeasures = 0;
  volatile bool triggered = false;
};

namespace buttons {

constexpr const size_t n = 4;

extern DebouncedTouchSensor sensors[n];

using Config = std::array<DebouncedTouchSensor::Config, n>;

/*
  Reinitialize the R4_Touch library with a new configuration.
*/
void reset(const Config &config);

/*
  This is implemented in src/main.cpp, and hooks the edge callbacks (that runs in an interrupt context) to the Submitter
  actions.
*/
void edgeCallback(size_t i, bool rising);

} // namespace buttons

} // namespace blastic
