#include "Buttons.h"

extern int num_configured_sensors;

namespace blastic {

bool DebouncedTouchSensor::updateRead() {
  lastMeasures <<= 1;
  lastMeasures |= read();
  constexpr const auto debounceBits = sizeof(lastMeasures) * 8;
  auto onBits = __builtin_popcount(lastMeasures);
  // if there are more than 3/4 of last measurement bits in the state opposite to the triggered flag, flip it
  if (4 * (!triggered ? onBits : debounceBits - onBits) >= 3 * debounceBits) {
    triggered = !triggered;
    return true;
  }
  return false;
}

void DebouncedTouchSensor::measurementCallback() {
  bool restartMeasurement = false;
  for (auto &sensor : buttons::sensors) {
    if (sensor.updateRead()) buttons::edgeCallback(&sensor - buttons::sensors, sensor);
    restartMeasurement |= sensor.lastMeasures;
  }
  // if any of the buttons is in a touch state, keep measuring without delay
  if (restartMeasurement) startTouchMeasurement(false);
}

namespace buttons {

DebouncedTouchSensor sensors[n];

void reset(const Config &configs) {
  static StaticTimer_t timerBuff;
  // this timer makes sure that we are measuring the capacitors every 50ms at least
  static TimerHandle_t timer = xTimerCreateStatic(
      "QECapMeasure", pdMS_TO_TICKS(50), true, nullptr, [](TimerHandle_t) { startTouchMeasurement(false); }, &timerBuff);
  xTimerStop(timer, portMAX_DELAY);
  stopTouchMeasurement();
  // we need to undo the changes to the CTSU made by setTouchMode
  for (auto &b : R_CTSU->CTSUCHAC) b = 0;
  num_configured_sensors = 0;
  // reconfigure all buttons
  for (int i = 0; i < n; i++) {
    auto &config = configs[i];
    sensors[i].begin(config.pin, config.threshold);
    sensors[i].applyPinSettings(config.settings);
  }
  attachMeasurementEndCallback(DebouncedTouchSensor::measurementCallback);
  xTimerReset(timer, portMAX_DELAY);
}

} // namespace buttons

} // namespace blastic
