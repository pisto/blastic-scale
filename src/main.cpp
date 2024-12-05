#include <iterator>
#include <cm_backtrace/cm_backtrace.h>
#include "blastic.h"
#include "SerialCliTask.h"
#include "Submitter.h"
#include "utils.h"

namespace blastic {

// initialize configuration with sane defaults
EEPROMConfig config = {
    .scale = {.dataPin = 5,
              .clockPin = 4,
              .mode = scale::HX711Mode::A128,
              .calibrations = {{.tareRawRead = 45527,
                                .weightRawRead = 114810,
                                .weight = 1.56}, // works for me, but not for thee
                               {.tareRawRead = 0, .weightRawRead = 0, .weight = 0.f},
                               {.tareRawRead = 0, .weightRawRead = 0, .weight = 0.f}}},
    // XXX GCC bug, cannot use initializer lists with strings
    .wifi = WifiConnection::EEPROMConfig{"", "", 10, 10},
    .submit =
        Submitter::EEPROMConfig{
            0.05, "", "",
            Submitter::EEPROMConfig::FormParameters{
                "docs.google.com/forms/d/e/1FAIpQLSeI3jofIWqtWghblVPOTO1BtUbE8KmoJsGRJuRAu2ceEMIJFw/formResponse",
                "entry.826036805", "entry.458823532", "entry.649832752", "entry.1219969504"}},
    .buttons = {
        {{.pin = 3,
          .threshold = 10000,
          .settings =
              {.div = CTSU_CLOCK_DIV_16, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 152, .count = 1}},
         {.pin = 8,
          .threshold = 10000,
          .settings =
              {.div = CTSU_CLOCK_DIV_16, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 202, .count = 1}},
         {.pin = 2,
          .threshold = 10000,
          .settings =
              {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 154, .count = 1}},
         {.pin = 6,
          .threshold = 10000,
          .settings = {
              .div = CTSU_CLOCK_DIV_16, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 282, .count = 1}}}}};

static Submitter &submitter();

using SerialCliTask = cli::SerialCliTask<Serial, 4 * 1024>;
static SerialCliTask &cliTask();

uint32_t debug = 0;

} // namespace blastic

/*
  Namespace for all the serial cli functions.
*/

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
  serial->print(s);
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
    MSerial()->print("failed to get measurements for tare\n");
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
    MSerial()->print("scale::calibrate: missing test weight argument\n");
    return;
  }
  char *weightEnd;
  auto weight = strtof(weightString, &weightEnd);
  if (weightString == weightEnd) {
    MSerial()->print("scale::calibrate: cannot parse test weight argument\n");
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

} // namespace scale

namespace wifi {

static void status(WordSplit &) {
  uint8_t status;
  char firmwareVersion[12];
  {
    MWiFi wifi;
    status = wifi->status();
    strcpy0(firmwareVersion, wifi->firmwareVersion());
  }
  MSerial serial;
  serial->print("wifi::status: status ");
  serial->print(status);
  serial->print(" version ");
  serial->println(firmwareVersion);
}

static void timeout(WordSplit &args) {
  if (auto timeoutString = args.nextWord()) {
    char *timeoutEnd;
    auto timeout = strtoul(timeoutString, &timeoutEnd, 10);
    if (timeoutString != timeoutEnd) config.wifi.disconnectTimeout = timeout;
  }
  MSerial serial;
  serial->print("wifi::timeout: ");
  serial->println(config.wifi.disconnectTimeout);
}

static void ssid(WordSplit &args) {
  auto ssid = args.rest(true, true);
  if (ssid) {
    strcpy0(config.wifi.ssid, ssid);
    memset(config.wifi.password, 0, sizeof(config.wifi.password));
  }
  MSerial serial;
  serial->print("wifi::ssid: ");
  if (strlen(config.wifi.ssid)) {
    serial->print('\'');
    serial->print(config.wifi.ssid);
    serial->print("\'\n");
  } else serial->print("<none>\n");
}

static void password(WordSplit &args) {
  if (auto password = args.rest(false, false)) strcpy0(config.wifi.password, password);
  MSerial serial;
  serial->print("wifi::password: ");
  if (strlen(config.wifi.password)) {
    serial->print('\'');
    serial->print(config.wifi.password);
    serial->print("\'\n");
  } else serial->print("<none>\n");
}

static void connect(WordSplit &) {
  {
    MWiFi wifi;
    if (strcmp(wifi->firmwareVersion(), WIFI_FIRMWARE_LATEST_VERSION)) {
      MSerial()->print("wifi::connect: bad wifi firmware, need version " WIFI_FIRMWARE_LATEST_VERSION "\n");
      return;
    }
  }
  if (!strlen(config.wifi.ssid)) {
    MSerial()->print("wifi::connect: configure the connection first with wifi::ssid\n");
    return;
  }
  uint8_t bssid[6];
  int32_t rssi;
  IPAddress ip, gateway, dns1, dns2;
  {
    WifiConnection wifi(config.wifi);
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

constexpr const uint16_t defaultTlsPort = 443;

static void ping(WordSplit &args) {
  auto address = args.nextWord();
  if (!address) {
    MSerial()->print("tls::ping: failed to parse address\n");
    return;
  }
  if (WifiConnection::ipConnectBroken) {
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
  {
    MWiFi wifi;
    if (strcmp(wifi->firmwareVersion(), WIFI_FIRMWARE_LATEST_VERSION)) {
      MSerial()->print("tls::ping: bad wifi firmware, need version " WIFI_FIRMWARE_LATEST_VERSION "\n");
      return;
    }
  }
  WifiConnection wifi(config.wifi);
  if (!wifi) {
    MSerial()->print("tls::ping: failed to connect to wifi\n");
    return;
  }
  MSerial()->print("tls::ping: connected to wifi\n");

  {
    blastic::WiFiSSLClient client;
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
    uint8_t tlsInput[maxLen];
    constexpr const unsigned int waitingReadInterval = 100;
    while (true) {
      // this is non blocking as the underlying code may return zero (and available() == 0) while still being connected
      auto len = client.read(tlsInput, maxLen);
      if (len < 0) break;
      if (!len) {
        vTaskDelay(pdMS_TO_TICKS(waitingReadInterval));
        continue;
      }
      MSerial()->write(tlsInput, len);
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
  if (auto collectionPoint = args.rest()) strcpy0(config.submit.collectionPoint, collectionPoint);
  MSerial serial;
  serial->print("submit::collectionPoint: ");
  if (strlen(config.submit.collectionPoint)) {
    serial->print('\'');
    serial->print(config.submit.collectionPoint);
    serial->print("\'\n");
  } else serial->print("<none>\n");
}

static void collectorName(WordSplit &args) {
  if (auto collectorName = args.rest()) strcpy0(config.submit.collectorName, collectorName);
  MSerial serial;
  serial->print("submit::collectorName: ");
  if (strlen(config.submit.collectorName)) {
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
  strcpy0(config.submit.collectionPoint, urn);
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

static constexpr const CliCallback callbacks[]{makeCliCallback(version),
                                               makeCliCallback(uptime),
                                               makeCliCallback(debug),
                                               makeCliCallback(scale::mode),
                                               makeCliCallback(scale::tare),
                                               makeCliCallback(scale::calibrate),
                                               makeCliCallback(scale::configuration),
                                               makeCliCallback(scale::raw),
                                               makeCliCallback(scale::weight),
                                               makeCliCallback(wifi::status),
                                               makeCliCallback(wifi::ssid),
                                               makeCliCallback(wifi::password),
                                               makeCliCallback(wifi::connect),
                                               makeCliCallback(tls::ping),
                                               makeCliCallback(submit::threshold),
                                               makeCliCallback(submit::collectionPoint),
                                               makeCliCallback(submit::collectorName),
                                               makeCliCallback(submit::urn),
                                               makeCliCallback(submit::action),
                                               CliCallback()};

} // namespace cli

namespace blastic {

static Submitter &submitter() {
  static Submitter submitter("Submitter", configMAX_PRIORITIES / 2);
  return submitter;
}

static SerialCliTask &cliTask() {
  static SerialCliTask cliTask(cli::callbacks);
  return cliTask;
}

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
  submitter();
  cliTask();
  buttons::reset(config.buttons);
  Serial.print("setup: done\n");
}

/*
  Hook failed assert to a Serial print, then throw a stack trace every 10 seconds.
*/

constexpr const int assertSleepMillis = 10000;

static void _assert_func_freertos(const char *file, int line, const char *failedExpression,
                                  const uint32_t (&stackTrace)[CMB_CALL_STACK_MAX_DEPTH], size_t stackDepth)
    [[noreturn]] {
  using namespace blastic;
  vTaskPrioritySet(nullptr, tskIDLE_PRIORITY + 1);
  while (true) {
    {
      MSerial serial;
      if (!*serial) serial->begin(BLASTIC_MONITOR_SPEED);
      while (!*serial);
      serial->print("assert: ");
      serial->print(file);
      serial->print(':');
      serial->print(line);
      serial->print(" failed expression ");
      serial->println(failedExpression);
      serial->print("assert: addr2line -e $FIRMWARE_FILE -a -f -C ");
      for (int i = 0; i < stackDepth; i++) {
        serial->print(' ');
        serial->print(stackTrace[i], 16);
      }
      serial->println();
    }
    vTaskDelay(pdMS_TO_TICKS(assertSleepMillis));
  }
}

static void _assert_func_arduino(const char *file, int line, const char *failedExpression,
                                 const uint32_t (&stackTrace)[CMB_CALL_STACK_MAX_DEPTH], size_t stackDepth)
    [[noreturn]] {
  if (!Serial) Serial.begin(BLASTIC_MONITOR_SPEED);
  while (!Serial);
  while (true) {
    Serial.print("assert: ");
    Serial.print(file);
    Serial.print(':');
    Serial.print(line);
    Serial.print(" failed expression ");
    Serial.println(failedExpression);
    Serial.print("assert: addr2line -e $FIRMWARE_FILE -a -f -C ");
    for (int i = 0; i < stackDepth; i++) {
      Serial.print(' ');
      Serial.print(stackTrace[i], 16);
    }
    Serial.println();
    delay(assertSleepMillis);
  }
}

// this is a weak function in the Arduino framework so we just need to declare it here without the -Wl,--wrap trick
extern "C" void __assert_func(const char *file, int line, const char *, const char *failedExpression)
    [[noreturn]] {
  uint32_t stackTrace[CMB_CALL_STACK_MAX_DEPTH] = {0};
  size_t stackDepth = cm_backtrace_call_stack(stackTrace, CMB_CALL_STACK_MAX_DEPTH, cmb_get_sp());
  (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING ? _assert_func_freertos : _assert_func_arduino)(
      file, line, failedExpression, stackTrace, stackDepth);
}
