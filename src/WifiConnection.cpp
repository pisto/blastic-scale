#include "blastic.h"
#include "StaticTask.h"

namespace wifi {

using namespace blastic;

const bool Layer3::ipConnectBroken = strcmp(WIFI_FIRMWARE_LATEST_VERSION, "0.4.2") <= 0;

bool Layer3::firmwareCompatible() {
  MWiFi wifi;
  return strcmp(wifi->firmwareVersion(), WIFI_FIRMWARE_LATEST_VERSION) >= 0;
}

util::Looper<1024> &Layer3::backgroundLoop() {
  static util::Looper<1024> backgroundLoop("backgroundLoop", tskIDLE_PRIORITY + 1);
  return backgroundLoop;
}

Layer3::Layer3(const Config &config) : util::Mutexed<::WiFi>(), skipDisconnectTimer(false) {
  if (!firmwareCompatible()) return;
  constexpr const uint32_t dhcpPollInterval = 100;
  auto &wifi = **this;
  wifi.end();
  if (wifi.begin(config.ssid, std::strlen(config.password) ? static_cast<const char *>(config.password) : nullptr) !=
      WL_CONNECTED)
    return;
  auto dhcpStart = millis();
  while (!wifi.localIP() && millis() - dhcpStart < config.dhcpTimeout * 1000) vTaskDelay(dhcpPollInterval);
}

Layer3::operator bool() const {
  auto &_this = *this;
  return _this->status() == WL_CONNECTED && _this->localIP() && _this->gatewayIP() && _this->dnsIP();
}

int SSLClient::read() {
  vTaskSuspendAll();
  int result = -1;
  if (connected()) result = ::WiFiSSLClient::read();
  xTaskResumeAll();
  return result;
}

int SSLClient::read(uint8_t *buf, size_t size) {
  vTaskSuspendAll();
  int result = -1;
  if (connected()) result = ::WiFiSSLClient::read(buf, size);
  xTaskResumeAll();
  return result;
}

Layer3::~Layer3() {
  static int lastUsage;
  lastUsage = millis();
  static StaticTimer_t disconnectTimerBuff;
  static TimerHandle_t disconnectTimer = xTimerCreateStatic(
      "WiFidisconnect", 1, false, nullptr,
      [](TimerHandle_t) {
        backgroundLoop().set(
            [](uint32_t) {
              Layer3 wifi(true);
              if (millis() - lastUsage > config.wifi.disconnectTimeout * 1000) wifi->end();
              return portMAX_DELAY;
            },
            0);
      },
      &disconnectTimerBuff);
  if (!skipDisconnectTimer)
    configASSERT(xTimerChangePeriod(disconnectTimer, pdMS_TO_TICKS(config.wifi.disconnectTimeout * 1000), portMAX_DELAY));
}

Layer3::Layer3() : util::Mutexed<::WiFi>(), skipDisconnectTimer(false) {}
Layer3::Layer3(bool) : util::Mutexed<::WiFi>(), skipDisconnectTimer(true) {}

} // namespace wifi
