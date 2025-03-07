#include <iterator>
#include <memory>
#include <base64.hpp>
#include "DataFlashBlockDevice.h"
#include "blastic.h"
#include "SerialCliTask.h"
#include "Submitter.h"
#include "utils.h"

namespace blastic {

uint32_t debug = 0;
Config config;

static Submitter &submitter() {
  static Submitter submitter("Submitter", configMAX_PRIORITIES / 2);
  return submitter;
}

using SerialCliTask = cli::SerialCliTask<Serial, 4 * 1024>;
static SerialCliTask &cliTask();

namespace buttons {

void edgeCallback(size_t i, bool rising) {
  // NB: this is run in an interrupt context, do not do anything heavyweight
  if (!rising) return;
  return submitter().action_ISR(std::get<Submitter::Action>(Submitter::actions[i + 1]));
}

} // namespace buttons
} // namespace blastic

void setup() {
  using namespace blastic;
  Serial.begin(BLASTIC_MONITOR_SPEED);
  while (!Serial);
  Serial.print("setup: booting blastic-scale version ");
  Serial.println(version);
  auto [ioret, configVersion] = config.load();
  switch (ioret) {
  case eeprom::IOret::UPGRADED: Serial.print("setup: eeprom saved config converted from older version\n");
  case eeprom::IOret::OK:
    Serial.print("setup: loaded configuration from eeprom version ");
    Serial.print(configVersion);
    Serial.println();
    break;
  default:
    config.defaults();
    Serial.print("setup: cannot load eeprom data, using defaults\n");
    break;
  }
  submitter();
  cliTask();
  buttons::reset(config.buttons);
  Serial.print("setup: done\n");
}

namespace cli {

using namespace blastic;

static void uptime(WordSplit &) {
  auto s = millis() / 1000;
  MSerial serial;
  serial->print("uptime: ");
  serial->print(s / 60 / 60 / 24);
  serial->print('d');
  serial->print(s / 60 / 60);
  serial->print('h');
  serial->print(s / 60);
  serial->print('m');
  serial->print(s % 60);
  serial->print("s\n");
}

static void version(WordSplit &ws) {
  MSerial serial;
  serial->print("version: ");
  serial->println(blastic::version);
  uptime(ws);
}

static void debug(WordSplit &args) {
  auto arg = args.nextWord() ?: "0";
  blastic::debug = atoi(arg);
  if (blastic::debug >= 2) modem.debug(Serial, 2);
  else modem.noDebug();
  MSerial serial;
  serial->print("debug: ");
  serial->println(blastic::debug);
}

#if (configUSE_TRACE_FACILITY == 1)

static void tasks(WordSplit &) {
  vTaskSuspendAll();
  const auto taskCount = uxTaskGetNumberOfTasks();
  auto tasks = std::make_unique<TaskStatus_t[]>(taskCount);
  bool ok = uxTaskGetSystemState(tasks.get(), taskCount, nullptr);
  xTaskResumeAll();
  MSerial serial;
  if (!ok) {
    serial->print("tasks: no tasks returned\n");
    return;
  }
  for (int i = 0; i < taskCount; i++) {
    serial->print("tasks: ");
    auto &task = tasks[i];
    serial->print(task.pcTaskName);
    serial->print(" state ");
    serial->print(task.eCurrentState);
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    serial->print(" high ");
    serial->print(task.usStackHighWaterMark);
#endif
    serial->println();
  }
}

#endif

static void sleep(WordSplit &args) {
  auto arg = args.nextWord() ?: "0";
  vTaskDelay(pdMS_TO_TICKS(atoi(arg) * 1000));
}

namespace scale {

using namespace blastic::scale;

constexpr const uint32_t scaleCliTimeout = 2000, scaleCliMaxMedianWidth = 16;

static void mode(WordSplit &args) {
  auto modeStr = args.nextWord();
  if (!modeStr) {
    MSerial()->print("scale::mode: missing mode argument\n");
    return;
  }
  auto hash = util::murmur3_32(modeStr);
  for (auto mode : modeHashes) {
    if (std::get<uint32_t>(mode) != hash) continue;
    config.scale.mode = std::get<HX711Mode>(mode);
    MSerial serial;
    serial->print("scale::mode: mode set to ");
    serial->println(modeStr);
    return;
  }
  MSerial()->print("scale::mode: mode not found\n");
}

static void tare(WordSplit &) {
  auto value = raw(config.scale, scaleCliMaxMedianWidth, pdMS_TO_TICKS(scaleCliTimeout));
  if (value == readErr) {
    MSerial()->print("scale::tare: failed to get measurements for tare\n");
    return;
  }
  auto &calibration = config.scale.getCalibration();
  calibration.tareRawRead = value;
  MSerial serial;
  serial->print("scale::tare: set to raw read value ");
  serial->println(value);
}

static void calibrate(WordSplit &args) {
  auto weightString = args.nextWord();
  if (!weightString) {
    MSerial()->print("scale::calibrate: missing probe weight argument\n");
    return;
  }
  char *weightEnd;
  auto weight = strtof(weightString, &weightEnd);
  if (weightString == weightEnd) {
    MSerial()->print("scale::calibrate: cannot parse probe weight argument\n");
    return;
  }
  auto value = raw(config.scale, scaleCliMaxMedianWidth, pdMS_TO_TICKS(scaleCliTimeout));
  if (value == readErr) {
    MSerial()->print("scale::calibrate: failed to get measurements for calibration\n");
    return;
  }
  auto &calibration = config.scale.getCalibration();
  calibration.weightRawRead = value, calibration.weight = weight;
  MSerial serial;
  serial->print("scale::calibrate: set to raw read value ");
  serial->println(value);
}

static void configuration(WordSplit &) {
  auto modeString = modeStrings[uint32_t(config.scale.mode)];
  auto &calibration = config.scale.getCalibration();
  MSerial serial;
  serial->print("scale::configuration: mode ");
  serial->print(modeString);
  serial->print(" tareRawRead weightRawRead weight ");
  serial->print(calibration.tareRawRead);
  serial->print(' ');
  serial->print(calibration.weightRawRead);
  serial->print(' ');
  serial->println(calibration.weight);
}

static void raw(WordSplit &args) {
  auto medianWidthArg = args.nextWord();
  auto medianWidth = min(max(1, medianWidthArg ? atoi(medianWidthArg) : 1), scaleCliMaxMedianWidth);
  auto value = blastic::scale::raw(config.scale, medianWidth, pdMS_TO_TICKS(scaleCliTimeout));
  MSerial serial;
  serial->print("scale::raw: ");
  value == readErr ? serial->print("HX711 error\n") : serial->println(value);
}

static void weight(WordSplit &args) {
  auto medianWidthArg = args.nextWord();
  auto medianWidth = min(max(1, medianWidthArg ? atoi(medianWidthArg) : 1), scaleCliMaxMedianWidth);
  auto value = blastic::scale::weight(config.scale, medianWidth, pdMS_TO_TICKS(scaleCliTimeout));
  MSerial serial;
  serial->print("scale::weight: ");
  if (value == weightCal) serial->print("uncalibrated\n");
  else if (value == weightErr) serial->print("HX711 error\n");
  else serial->println(value);
}

static void fake(WordSplit &args) {
  auto fake = args.nextWord();
  if (fake) scale::debug::fake = atoi(fake);
  MSerial serial;
  serial->print("scale::fake: ");
  serial->println(scale::debug::fake);
}

} // namespace scale

namespace wifi {

using namespace ::wifi;

static void status(WordSplit &) {
  uint8_t status;
  util::StringBuffer<12> firmwareVersion;
  {
    MWiFi wifi;
    status = wifi->status();
    firmwareVersion = wifi->firmwareVersion();
  }
  MSerial serial;
  serial->print("wifi::status: status ");
  serial->print(status);
  serial->print(" version ");
  serial->println(firmwareVersion);
}

static void idle(WordSplit &args) {
  if (auto timeoutString = args.nextWord()) {
    char *timeoutEnd;
    auto idle = strtoul(timeoutString, &timeoutEnd, 10);
    if (timeoutString != timeoutEnd) config.wifi.idleTimeout = idle;
  }
  MSerial serial;
  serial->print("wifi::idle: ");
  serial->println(config.wifi.idleTimeout);
}

static void ssid(WordSplit &args) {
  auto ssid = args.rest(true, true);
  if (ssid) {
    config.wifi.ssid = ssid;
    memset(config.wifi.password, 0, sizeof(config.wifi.password));
  }
  MSerial serial;
  serial->print("wifi::ssid: ");
  if (std::strlen(config.wifi.ssid)) serial->println(config.wifi.ssid);
  else serial->print("<none>\n");
}

static void password(WordSplit &args) {
  if (auto password = args.rest(false, false)) config.wifi.password = password;
  MSerial serial;
  serial->print("wifi::password: ");
  if (std::strlen(config.wifi.password)) serial->println(config.wifi.password);
  else serial->print("<none>\n");
}

static void connect(WordSplit &) {
  if (!Layer3::firmwareCompatible()) {
    MSerial()->print("wifi::connect: bad wifi firmware, need at least version " WIFI_FIRMWARE_LATEST_VERSION "\n");
    return;
  }
  if (!std::strlen(config.wifi.ssid)) {
    MSerial()->print("wifi::connect: configure the connection first with wifi::ssid\n");
    return;
  }
  uint8_t bssid[6];
  int32_t rssi;
  IPAddress ip, gateway, dns1, dns2;
  {
    Layer3 wifi(config.wifi);
    auto status = wifi->status();
    if (status != WL_CONNECTED) {
      MSerial serial;
      serial->print("wifi::connect: connection failed (");
      serial->print(status);
      serial->print(")\n");
      return;
    }
    MSerial()->print("wifi::connect: connected\n");
    wifi->BSSID(bssid);
    rssi = wifi->RSSI();
    ip = wifi->localIP(), gateway = wifi->gatewayIP(), dns1 = wifi->dnsIP(0), dns2 = wifi->dnsIP(1);
  }

  MSerial serial;
  serial->print("wifi::connect: bssid ");
  for (auto b : bssid) serial->print(b, 16);
  serial->print(" rssi ");
  serial->print(rssi);
  serial->print("dBm ip ");
  serial->print(ip);
  serial->print(" gateway ");
  serial->print(gateway);
  serial->print(" dns1 ");
  serial->print(dns1);
  serial->print(" dns2 ");
  serial->println(dns2);
}

} // namespace wifi

namespace tls {

using namespace ::wifi;

constexpr const uint16_t defaultTlsPort = 443;

static void ping(WordSplit &args) {
  auto address = args.nextWord();
  if (!address) {
    MSerial()->print("tls::ping: failed to parse address\n");
    return;
  }
  if (Layer3::ipConnectBroken) {
    IPAddress ip;
    if (ip.fromString(address)) {
      MSerial()->print("tls::ping: tls validation is broken as of firmware version " WIFI_FIRMWARE_LATEST_VERSION
                       " for direct to IP connections, giving up\n");
      return;
    }
  }
  auto portString = args.nextWord();
  char *portEnd;
  auto port = strtoul(portString, &portEnd, 10);
  if (portString == portEnd) port = defaultTlsPort;
  if (!port || port > uint16_t(-1)) {
    MSerial()->print("tls::ping: invalid port\n");
    return;
  }
  if (!Layer3::firmwareCompatible()) {
    MSerial()->print("tls::ping: bad wifi firmware, need at least version " WIFI_FIRMWARE_LATEST_VERSION "\n");
    return;
  }
  Layer3 wifi(config.wifi);
  if (!wifi) {
    MSerial()->print("tls::ping: failed to connect to wifi\n");
    return;
  }
  MSerial()->print("tls::ping: connected to wifi\n");

  {
    SSLClient client;
    if (!client.connect(address, port)) {
      MSerial()->print("tls::ping: failed to connect to server\n");
      return;
    }
    MSerial()->print("tls::ping: connected to server\n");

    for (auto word = args.nextWord(); word;) {
      if (!client.print(' ') || !client.print(word)) goto sendError;
      if (!(word = args.nextWord())) {
        if (!client.println()) goto sendError;
        break;
      }
      continue;
    sendError:
      MSerial()->print("tls::ping: failed to write all the data\n");
      return;
    }
    MSerial()->print("tls::ping: send complete, waiting for response\n");

    constexpr const size_t maxLen = std::min(255, SERIAL_BUFFER_SIZE - 1);
    std::unique_ptr<uint8_t[]> tlsInput(new uint8_t[maxLen]);
    constexpr const unsigned int waitingReadInterval = 100;
    while (true) {
      // this is non blocking as the underlying code may return zero (and available() == 0) while still being connected
      auto len = client.read(tlsInput.get(), maxLen);
      if (len < 0) break;
      if (!len) {
        vTaskDelay(pdMS_TO_TICKS(waitingReadInterval));
        continue;
      }
      MSerial()->write(tlsInput.get(), len);
    }
  }

  MSerial()->print("\ntls::ping: connection closed\n");
}

} // namespace tls

namespace submit {

static void threshold(WordSplit &args) {
  if (auto thresholdString = args.nextWord()) {
    char *thresholdEnd;
    auto threshold = strtof(thresholdString, &thresholdEnd);
    if (thresholdString != thresholdEnd) config.submit.threshold = threshold;
  }
  MSerial serial;
  serial->print("submit::threshold: ");
  serial->println(config.submit.threshold, 3);
}

static void collectionPoint(WordSplit &args) {
  if (auto collectionPoint = args.rest()) config.submit.collectionPoint = collectionPoint;
  MSerial serial;
  serial->print("submit::collectionPoint: ");
  if (std::strlen(config.submit.collectionPoint)) {
    serial->print('\'');
    serial->print(config.submit.collectionPoint);
    serial->print("\'\n");
  } else serial->print("<none>\n");
}

static void collectorName(WordSplit &args) {
  if (auto collectorName = args.rest()) config.submit.collectorName = collectorName;
  MSerial serial;
  serial->print("submit::collectorName: ");
  if (std::strlen(config.submit.collectorName)) {
    serial->print('\'');
    serial->print(config.submit.collectorName);
    serial->print("\'\n");
  } else serial->print("<none>\n");
}

static void urn(WordSplit &args) {
  auto urn = args.nextWord();
  if (!urn) {
    MSerial()->print("submit::urn: missing urn\n");
    return;
  }
  if (strstr(urn, "https://") == urn || strstr(urn, "http://") == urn) {
    MSerial()->print("submit::urn: specify the urn argument without http:// or https://\n");
    return;
  }
  config.submit.collectionPoint = urn;
  MSerial serial;
  serial->print("submit::urn: collection point ");
  serial->println(config.submit.form.urn);
}

static void action(WordSplit &args) {
  auto actionStr = args.nextWord();
  if (!actionStr) MSerial()->print("submit::action: missing command argument\n");
  auto hash = util::murmur3_32(actionStr);
  for (auto action : Submitter::actions) {
    if (std::get<uint32_t>(action) != hash) continue;
    submitter().action(std::get<Submitter::Action>(action));
    MSerial serial;
    serial->print("submit::action: sent action ");
    serial->println(actionStr);
    return;
  }
  MSerial()->print("submit::action: action not found\n");
}

} // namespace submit

namespace eeprom {

static void save(WordSplit &) {
  auto result = config.save();
  MSerial serial;
  serial->print("eeprom::save: ");
  if (result == blastic::eeprom::IOret::OK) {
    serial->print("ok ");
    serial->print(sizeof(config));
    serial->print(" bytes\n");
  } else serial->print("error\n");
}

static void export_(WordSplit &) {
  auto inputLen = blastic::eeprom::maxConfigLength, base64Length = (inputLen + 2 - ((inputLen + 2) % 3)) / 3 * 4 + 1;
  configASSERT(base64Length == encode_base64_length(inputLen) + 1);
  unsigned char input[inputLen], base64[base64Length];
  auto &flash = DataFlashBlockDevice::getInstance();
  if (flash.read(input, 0, inputLen)) {
    MSerial()->print("eeprom::export: read error\n");
    return;
  }
  encode_base64(input, inputLen, base64);
  MSerial()->println(reinterpret_cast<char *>(base64));
}

static void defaults(WordSplit &args) {
  auto defaults = std::make_unique<Config>();
  defaults->defaults();
  auto result = defaults->save();
  MSerial serial;
  serial->print("eeprom::defaults: ");
  if (result == blastic::eeprom::IOret::OK) {
    serial->print("ok ");
    serial->print(sizeof(defaults));
    serial->print(" bytes\n");
  } else serial->print("error\n");
}

static void blank(WordSplit &args) {
  auto &flash = DataFlashBlockDevice::getInstance();
  if (!flash.erase(0, blastic::eeprom::maxConfigLength)) {
    MSerial serial;
    serial->print("eeprom::blank: ok ");
    serial->print(blastic::eeprom::maxConfigLength);
    serial->print(" bytes\n");
  } else MSerial()->print("eeprom::blank: error\n");
}

} // namespace eeprom

namespace sd {

static void probe(WordSplit &) {
  bool ok;
  uint8_t status, error, type;
  {
    SDCard sd(config.sdcard.CSPin);
    ok = sd;
    auto &card = (*sd).*get(util::SDClassBackdoor());
    status = card.errorCode();
    error = card.errorData();
    type = card.type();
  }
  MSerial serial;
  serial->print("sd::probe: ");
  if (ok) {
    serial->print("ok type ");
    serial->println(type);
  } else {
    serial->print("error status ");
    serial->print(status);
    serial->print(' ');
    serial->println(error);
  }
}

} // namespace sd

namespace ntp {

static void hostname(WordSplit &args) {
  auto hostname = args.nextWord();
  if (hostname && *hostname) config.ntp.hostname = hostname;
  MSerial serial;
  serial->print("ntp::hostname: ");
  serial->println(config.ntp.hostname);
}

static void epoch(WordSplit &) {
  int epoch = ::ntp::unixTime();
  MSerial serial;
  serial->print("ntp::epoch: ");
  serial->println(epoch);
}

static void sync(WordSplit &) {
  using namespace wifi;
  if (!Layer3::firmwareCompatible()) {
    MSerial()->print("ntp::sync: bad wifi firmware, need at least version " WIFI_FIRMWARE_LATEST_VERSION "\n");
    return;
  }
  if (!std::strlen(config.wifi.ssid)) {
    MSerial()->print("ntp::sync: configure the connection first with wifi::ssid\n");
    return;
  }
  {
    Layer3 l3(config.wifi);
    if (!l3) {
      MSerial()->print("ntp::sync: failed to connect to wifi\n");
      return;
    }
  }
  ::ntp::startSync(true);
  MSerial()->print("ntp::sync: started sync\n");
}

} // namespace ntp

static constexpr const CliCallback callbacks[]{makeCliCallback(version),
                                               makeCliCallback(uptime),
                                               makeCliCallback(debug),
#if (configUSE_TRACE_FACILITY == 1)
                                               makeCliCallback(tasks),
#endif
                                               makeCliCallback(sleep),
                                               makeCliCallback(scale::mode),
                                               makeCliCallback(scale::tare),
                                               makeCliCallback(scale::calibrate),
                                               makeCliCallback(scale::configuration),
                                               makeCliCallback(scale::raw),
                                               makeCliCallback(scale::weight),
                                               makeCliCallback(scale::fake),
                                               makeCliCallback(wifi::status),
                                               makeCliCallback(wifi::idle),
                                               makeCliCallback(wifi::ssid),
                                               makeCliCallback(wifi::password),
                                               makeCliCallback(wifi::connect),
                                               makeCliCallback(tls::ping),
                                               makeCliCallback(submit::threshold),
                                               makeCliCallback(submit::collectionPoint),
                                               makeCliCallback(submit::collectorName),
                                               makeCliCallback(submit::urn),
                                               makeCliCallback(submit::action),
                                               makeCliCallback(eeprom::save),
                                               CliCallback("eeprom::export", eeprom::export_),
                                               makeCliCallback(eeprom::defaults),
                                               makeCliCallback(eeprom::blank),
                                               makeCliCallback(sd::probe),
                                               makeCliCallback(ntp::hostname),
                                               makeCliCallback(ntp::epoch),
                                               makeCliCallback(ntp::sync),
                                               CliCallback()};

} // namespace cli

namespace blastic {

static SerialCliTask &cliTask() {
  static SerialCliTask cliTask(cli::callbacks);
  return cliTask;
}

} // namespace blastic
