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
  buttons::reload(config.buttons);
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

static void tare(WordSplit &) {
  auto value = raw(config.scale, scaleCliMaxMedianWidth, pdMS_TO_TICKS(scaleCliTimeout));
  if (value == readErr) {
    MSerial()->print("scale::tare: failed to get measurements for tare\n");
    return;
  }
  auto &calibration = config.scale.getCalibration();
  calibration.tareRead = value;
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
  calibration.calibrationRead = value, calibration.calibrationWeight.f = weight;
  MSerial serial;
  serial->print("scale::calibrate: set to raw read value ");
  serial->println(value);
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

static void connect(WordSplit &) {
  if (!Layer3::firmwareCompatible()) {
    MSerial()->print("wifi::connect: bad wifi firmware, need at least version " WIFI_FIRMWARE_LATEST_VERSION "\n");
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

constexpr const uint16_t defaultTlsPort = 443;

static void tls(WordSplit &args) {
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
      // this is non blocking as the underlying code may return zero (and available() == 0) while still connected
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

} // namespace wifi

namespace submit {

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

namespace buttons {

static void reload(WordSplit &) {
  blastic::buttons::reload(blastic::config.buttons);
  MSerial()->print("buttons::reload: reloaded configuration\n");
}

} // namespace buttons

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

template <bool get> void accessor(WordSplit &args);

static constexpr const CliCallback callbacks[]{makeCliCallback(uptime),
                                               CliCallback("get", accessor<true>),
                                               CliCallback("set", accessor<false>),
#if (configUSE_TRACE_FACILITY == 1)
                                               makeCliCallback(tasks),
#endif
                                               makeCliCallback(sleep),
                                               makeCliCallback(scale::tare),
                                               makeCliCallback(scale::calibrate),
                                               makeCliCallback(scale::raw),
                                               makeCliCallback(scale::weight),
                                               makeCliCallback(wifi::status),
                                               makeCliCallback(wifi::connect),
                                               makeCliCallback(wifi::tls),
                                               makeCliCallback(submit::action),
                                               makeCliCallback(buttons::reload),
                                               makeCliCallback(eeprom::save),
                                               CliCallback("eeprom::export", eeprom::export_),
                                               makeCliCallback(eeprom::blank),
                                               makeCliCallback(sd::probe),
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
