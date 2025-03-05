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
  static StaticTimer_t ntpTimerBuff;
  static TimerHandle_t ntpTimer =
      xTimerCreateStatic("ntpRefresh", 1, false, nullptr, [](TimerHandle_t) { startSync(); }, &ntpTimerBuff);
  configASSERT(blastic::config.ntp.refresh
                   ? xTimerChangePeriod(ntpTimer, pdMS_TO_TICKS((blastic::config.ntp.refresh + 1) * 1000), portMAX_DELAY)
                   : xTimerStop(ntpTimer, portMAX_DELAY));
  auto now = unixTime();
  if (!force && blastic::config.ntp.refresh && now && now - lastSyncEpoch < blastic::config.ntp.refresh) return;
  using namespace wifi;
  Layer3::background().set(
      [hostname = String(blastic::config.ntp.hostname)](uint32_t) {
        Layer3 wifi;
        if (!wifi) return blastic::MSerial()->print("ntpsync: no wifi connection\n"), portMAX_DELAY;
        auto udp = std::make_unique<WiFiUDP>();
        auto ntp = std::make_unique<NTPClient>(*udp, hostname.c_str());
        ntp->begin();
        ntp->forceUpdate();
        ntp->end();
        if (!ntp->isTimeSet()) return blastic::MSerial()->print("ntpsync: failed to sync\n"), portMAX_DELAY;
        lastSyncEpoch = ntp->getEpochTime();
        offsetToUnixTime = lastSyncEpoch - updateRealTimeSeconds();
        // call updateRealTimeSeconds() every day to avoid millis() overflow issues
        static StaticTimer_t rtcTimerBuff;
        static TimerHandle_t rtcTimer = xTimerCreateStatic(
            "rtcRefresh", pdMS_TO_TICKS(60 * 24 * 24 * 1000), true, nullptr,
            [](TimerHandle_t) { updateRealTimeSeconds(); }, &rtcTimerBuff);
        configASSERT(xTimerStart(rtcTimer, portMAX_DELAY));
        blastic::MSerial serial;
        serial->print("ntpsync: synced at ");
        serial->println(lastSyncEpoch);
        return portMAX_DELAY;
      },
      force ? portMAX_DELAY : 0);
}

} // namespace ntp
