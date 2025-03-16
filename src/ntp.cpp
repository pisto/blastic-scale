#include <memory>
#include "ntp.h"
#include "blastic.h"
#include <NTPClient.h>

namespace {

int oldSeenMillis = 0, realTimeSeconds = 0, offsetToUnixTime, lastSyncEpoch = 0;

int updateRealTimeSeconds() {
  util::Mutexed<realTimeSeconds> lockedRTS;
  int now = millis();
  *lockedRTS += (now - oldSeenMillis) / 1000;
  oldSeenMillis = now;
  return *lockedRTS;
}

} // namespace

namespace ntp {

int unixTime() { return offsetToUnixTime ? updateRealTimeSeconds() + offsetToUnixTime : 0; }

void startSync(bool force) {
  using namespace blastic;
  // call updateRealTimeSeconds() every day to avoid millis() overflow issues
  static StaticTimer_t rtcTimerBuff;
  static TimerHandle_t rtcTimer = xTimerCreateStatic(
      "rtcRefresh", pdMS_TO_TICKS(60 * 24 * 24 * 1000), true, nullptr, [](TimerHandle_t) { updateRealTimeSeconds(); },
      &rtcTimerBuff);
  configASSERT(xTimerStart(rtcTimer, portMAX_DELAY));
  auto now = unixTime();
  if (!strlen(config.ntp.hostname) || (!force && config.ntp.refresh && now && now - lastSyncEpoch < config.ntp.refresh))
    return;
  using namespace wifi;
  Layer3::background().set(
      [hostname = String(config.ntp.hostname)](uint32_t) {
        Layer3 wifi;
        if (!wifi) {
          MSerial()->print("ntpsync: no wifi connection\n");
          return portMAX_DELAY;
        }
        auto udp = std::make_unique<WiFiUDP>();
        auto ntp = std::make_unique<NTPClient>(*udp, hostname.c_str());
        ntp->begin();
        ntp->forceUpdate();
        ntp->end();
        if (!ntp->isTimeSet()) {
          MSerial()->print("ntpsync: failed to sync\n");
          return portMAX_DELAY;
        }
        lastSyncEpoch = ntp->getEpochTime();
        offsetToUnixTime = lastSyncEpoch - updateRealTimeSeconds();
        MSerial serial;
        serial->print("ntpsync: synced at ");
        serial->println(lastSyncEpoch);
        return portMAX_DELAY;
      },
      force ? portMAX_DELAY : 0);
}

} // namespace ntp
