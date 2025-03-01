#include "blastic.h"
#include <NTPClient.h>

namespace {

int oldSeenMillis = 0, realTimeSeconds = 0, offsetToUnixTime, lastSyncEpoch;

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

void startSync(const Config &config, bool force) {
  if (!force && lastSyncEpoch && unixTime() - lastSyncEpoch < 24 * 60 * 60) return;
  using namespace wifi;
  Layer3::background().set(
      [config](uint32_t) {
        Layer3 wifi;
        if (!wifi) return blastic::MSerial()->print("ntpsync: no wifi connection\n"), portMAX_DELAY;
        WiFiUDP udp;
        NTPClient ntp(udp, config.hostname);
        ntp.begin();
        ntp.forceUpdate();
        ntp.end();
        if (!ntp.isTimeSet()) return blastic::MSerial()->print("ntpsync: failed to sync\n"), portMAX_DELAY;
        lastSyncEpoch = ntp.getEpochTime();
        offsetToUnixTime = lastSyncEpoch - updateRealTimeSeconds();
        blastic::MSerial serial;
        serial->print("ntpsync: synced at ");
        serial->println(lastSyncEpoch);
        return portMAX_DELAY;
      },
      force ? portMAX_DELAY : 0);
}

} // namespace ntp
