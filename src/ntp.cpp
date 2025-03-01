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
  auto now = unixTime();
  if (!force && now && now - lastSyncEpoch < 24 * 60 * 60) return;
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
        blastic::MSerial serial;
        serial->print("ntpsync: synced at ");
        serial->println(lastSyncEpoch);
        return portMAX_DELAY;
      },
      force ? portMAX_DELAY : 0);
}

} // namespace ntp
