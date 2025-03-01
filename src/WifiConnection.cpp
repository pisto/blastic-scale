#include "blastic.h"
#include "StaticTask.h"

namespace wifi {

using namespace blastic;

const bool Layer3::ipConnectBroken = strcmp(WIFI_FIRMWARE_LATEST_VERSION, "0.4.2") <= 0;

bool Layer3::firmwareCompatible() {
  MWiFi wifi;
  return strcmp(wifi->firmwareVersion(), WIFI_FIRMWARE_LATEST_VERSION) >= 0;
}

util::Looper<1024> &Layer3::background() {
  static util::Looper<1024> background("Layer3Background", tskIDLE_PRIORITY + 1);
  return background;
}

Layer3::Layer3(const Config &config) : util::Mutexed<::WiFi>(), backgroundJob(false) {
  if (!firmwareCompatible()) return;
  constexpr const uint32_t dhcpPollInterval = 100;
  auto &wifi = **this;
  wifi.end();
  if (wifi.begin(config.ssid, std::strlen(config.password) ? static_cast<const char *>(config.password) : nullptr) !=
      WL_CONNECTED)
    return;
  auto dhcpStart = millis();
  while (!*this && millis() - dhcpStart < config.dhcpTimeout * 1000) vTaskDelay(dhcpPollInterval);
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
  if (backgroundJob) return;
  static int lastUsage;
  lastUsage = millis();
  static StaticTimer_t disconnectTimerBuff;
  static TimerHandle_t disconnectTimer = xTimerCreateStatic(
      "WiFidisconnect", 1, false, nullptr,
      [](TimerHandle_t) {
        background().set(
            [](uint32_t) {
              Layer3 wifi;
              if (millis() - lastUsage > config.wifi.idleTimeout * 1000) {
                wifi->end();
                if (debug) MSerial()->print("wifi::idle: disconnected\n");
              }
              return portMAX_DELAY;
            },
            0);
      },
      &disconnectTimerBuff);
  configASSERT(xTimerChangePeriod(disconnectTimer, pdMS_TO_TICKS(config.wifi.idleTimeout * 1000), portMAX_DELAY));
}

Layer3::Layer3() : util::Mutexed<::WiFi>(), backgroundJob(true) {}

} // namespace wifi
