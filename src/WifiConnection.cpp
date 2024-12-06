#include "blastic.h"
#include "StaticTask.h"

namespace blastic {

const bool WifiConnection::ipConnectBroken = strcmp(WIFI_FIRMWARE_LATEST_VERSION, "0.4.2") <= 0;

// these variables must be accessed while holding the MWifi/WifiConnection mutex
static struct {
  uint8_t disconnectTimeout = 0;
  int endTime = 0;
} wifiReaper;

static void wifiReaperLoop() [[noreturn]] {
  ulTaskNotifyTakeIndexed(0, true, portMAX_DELAY);
  while (true) {
    int now;
    // copies of global vars as seen when mutex was held
    decltype(blastic::wifiReaper) wifiReaper;
    {
      MWiFi wifi;
      if (!blastic::wifiReaper.endTime) return wifiReaperLoop();
      now = millis();
      if (blastic::wifiReaper.endTime - now < 0) {
        wifi->end();
        blastic::wifiReaper.endTime = 0;
      }
      wifiReaper = blastic::wifiReaper;
    }
    if (!wifiReaper.endTime) {
      MSerial()->print("wifi: disconneted\n");
      return wifiReaperLoop();
    }
    ulTaskNotifyTakeIndexed(0, true, max(pdMS_TO_TICKS(wifiReaper.endTime - now), 1));
  }
}

WifiConnection::WifiConnection(const Config &config) : util::Mutexed<WiFi>() {
  constexpr const uint32_t dhcpPollInterval = 100;
  auto &wifi = *this;
  configASSERT(!strcmp(wifi->firmwareVersion(), WIFI_FIRMWARE_LATEST_VERSION));
  if (*this && !strncmp(wifi->SSID(), config.ssid, sizeof(config.ssid))) return;
  wifi->end();
  wifiReaper = {.disconnectTimeout = config.disconnectTimeout, .endTime = 0};
  if (wifi->begin(config.ssid, strnlen(config.password, sizeof(config.password)) ? config.password : nullptr) !=
      WL_CONNECTED)
    return;
  auto dhcpStart = millis();
  while (!wifi->localIP() && millis() - dhcpStart < config.dhcpTimeout * 1000) vTaskDelay(dhcpPollInterval);
}

WifiConnection::operator bool() const {
  auto &_this = *this;
  return _this->status() == WL_CONNECTED && _this->localIP() && _this->gatewayIP();
}

int WiFiSSLClient::read() {
  vTaskSuspendAll();
  int result = -1;
  if (connected()) result = ::WiFiSSLClient::read();
  xTaskResumeAll();
  return result;
}

int WiFiSSLClient::read(uint8_t *buf, size_t size) {
  vTaskSuspendAll();
  int result = -1;
  if (connected()) result = ::WiFiSSLClient::read(buf, size);
  xTaskResumeAll();
  return result;
}

WifiConnection::~WifiConnection() {
  if (!wifiReaper.disconnectTimeout) return;
  wifiReaper.endTime = millis() + wifiReaper.disconnectTimeout * 1000;
  static util::StaticTask<1024> timeoutTask(wifiReaperLoop, "WiFiReaper");
  xTaskNotifyGiveIndexed(timeoutTask, 0);
}

} // namespace blastic
