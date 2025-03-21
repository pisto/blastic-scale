#include <string>
#include <array>
#include <memory>
#include "blastic.h"
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include <ArduinoHttpClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "utils.h"
#include "SDCard.h"

/*
  Annoyingly, the ArduinoLEDMatrix timer interrupt cannot be stopped.
  Here use some magic to access its FspTimer member to be able to stop it manually on demand.
*/

ClassPrivateMemberAccessor(ArduinoLEDMatrix, FspTimer, _ledTimer);

namespace blastic {

static ArduinoLEDMatrix matrix;
static const auto &font = Font_4x6;
static constexpr const int matrixWidth = 12, matrixHeight = 8, fullCharsOnMatrix = matrixWidth / 4;

static util::loopFunction clear() {
  return +[](uint32_t &) {
    matrix.clear();
    memset(framebuffer, 0, sizeof(framebuffer));
    return portMAX_DELAY;
  };
}

/*
  Show a text line on the display, scrolling if necessary.
*/

static util::loopFunction scroll(std::string &&str, unsigned int initialDelay = 1000, unsigned int scrollDelay = 100,
                                 unsigned int blinkPeriods = 0) {
  if (!str.size()) return clear();
  int textWidht = str.size() * font.width;
  return [=, str = std::move(str), blinkCounter = 0](uint32_t &counter) mutable {
    int shiftX = scrollDelay && textWidht > matrixWidth ? -int(counter) : 0,
        wrapShiftX = shiftX + textWidht + matrixWidth / 2;
    matrix.clear();
    if (blinkPeriods && (blinkCounter++ / blinkPeriods) & 1) {
      memset(framebuffer, 0, sizeof(framebuffer));
      goto end;
    }
    if (shiftX + textWidht > 0) {
      matrix.beginText(shiftX, 1);
      matrix.print(str.c_str());
      matrix.endText();
    }
    if (scrollDelay && textWidht > matrixWidth && wrapShiftX < matrixWidth) {
      matrix.beginText(wrapShiftX, 1);
      matrix.print(str.c_str());
      matrix.endText();
    }
  end:
    if (!scrollDelay) return portMAX_DELAY;
    if (!counter) return pdMS_TO_TICKS(initialDelay);
    // if we just drew the wrapShiftX string as if it was the shiftX string with shiftX = 0, wrap the counter
    if (!wrapShiftX) counter = 0;
    return pdMS_TO_TICKS(scrollDelay);
  };
}

/*
  Show a float absolute value on screen.

  This function is optimized for small displays. On the Arduino UNO R4 WiFi, it shows 3 most significant digits.
  Points are drawn to indicate the order: the normal decimal point is drawn below the text line.

  If the position of the decimal point would be beyond the led matrix width (very large or very small
  numbers), additional leds are turned on. With 3 digits, there are 2 cases:
  - numbers >= 1000: additional points are shown for each missing integer digits. For example, 4267 is shown as "426"
  plus 2 points to the right
  - numbers < 0.01: additional points are shown on the left in addition to the normal floating decimal point. For
  example, 0.00543 is shown as "543" with 3 dots to the left.
*/

static util::loopFunction show(util::AnnotatedFloat v) {
  if (isnan(v)) return scroll("nan");
  if (isinf(v)) return scroll(v > 0 ? "+inf" : "-inf");
  float av = abs(v);
  constexpr const float flushThreshold = 0.000001;
  if (av < flushThreshold) return scroll("0");

  constexpr const auto digits = fullCharsOnMatrix < 7 ? fullCharsOnMatrix : 7;
  RingBufferN<digits> integerReverseStr;
  int order = -1;
  float iav, fav = modf(av, &iav);
  // get integer part digits in reverse order from units
  for (uint32_t iiav = iav; iiav >= 1; iiav /= 10, order++) {
    if (integerReverseStr.isFull()) integerReverseStr.read_char();
    integerReverseStr.store_char('0' + (iiav % 10));
  }
  std::array<char, digits + 1> str;
  str[digits] = '\0';
  auto strStart = str.data(), fractionalPtr = strStart + integerReverseStr.available();
  if (integerReverseStr.available()) {
    // weight has an integer part, flush it to the string
    for (auto integerPtr = fractionalPtr - 1; integerPtr >= strStart; integerPtr--)
      *integerPtr = integerReverseStr.read_char();
    fav *= 10;
  } else
    while ((fav *= 10) < 1) order--; // there is no integer part, find the first non-zero digit
  // loop over the fractional part (fav) digits
  for (auto digitsEnd = strStart + digits; fractionalPtr < digitsEnd; fractionalPtr++, fav = modf(fav, &iav) * 10)
    *fractionalPtr = '0' + uint8_t(fav);
  // with the 4x6 font numbers are actually 3x5, so align dots at Y offset 6
  auto textYOffset = 1, dotsYOffset = textYOffset + font.height;

  return [=](uint32_t &) {
    matrix.clear();
    matrix.beginText(0, textYOffset);
    matrix.print(str.data());
    matrix.endText();
    matrix.beginDraw();
    // floating decimal dot
    matrix.set((order + 1) * font.width, dotsYOffset, 1, 1, 1);
    // powers of 1/10, dots on the left
    for (int i = 0; i < max(-order, 0); i++) matrix.set(i, dotsYOffset, 1, 1, 1);
    // powers of 10, dots on the right
    for (int i = 0; i < max(order + 1 - fullCharsOnMatrix, 0); i++)
      matrix.set(matrixWidth - 1 - i, dotsYOffset, 1, 1, 1);
    matrix.endDraw();
    return portMAX_DELAY;
  };
}

/*
  This function records the last user input. lastInteractionMillis can be used to check for timeouts in the UI.
*/

void Submitter::gotInput() { lastInteractionMillis = millis(); }

/*
  Idle loop: show nothing, measure weight every 2 seconds.
*/

Submitter::Action Submitter::idling() {
  painter = clear();
  constexpr const auto idleWeightInterval = 2000;
  while (true) {
    uint32_t cmd;
    float weight;
    if (xTaskNotifyWait(0, -1, &cmd, pdMS_TO_TICKS(idleWeightInterval))) return toAction(cmd);
    weight = scale::weight(config.scale, 1, pdMS_TO_TICKS(1000));
    if (abs(weight) >= config.submit.threshold) {
      gotInput();
      return Action::NONE;
    }
  }
}

constexpr const auto idleTimeout = 60000;

/*
  Preview weight loop: show weight live, measure continuously, as HX711 allows.
*/

HasTimedOut<Submitter::Action> Submitter::preview() {
  auto prevWeight = util::AnnotatedFloat("n/a");
  for (; millis() - lastInteractionMillis < idleTimeout;) {
    uint32_t cmd;
    if (xTaskNotifyWait(0, -1, &cmd, 0)) return toAction(cmd);
    auto weight = scale::weight(config.scale, 1, pdMS_TO_TICKS(1000));
    if (abs(weight) < config.submit.threshold) weight.f = 0;
    else gotInput();
    if (weight == prevWeight) continue;
    prevWeight = weight;
    if (weight == scale::weightCal) painter = scroll("uncalibrated");
    else if (weight == scale::weightErr) painter = scroll("sensor error");
    else if (weight == 0) painter = scroll("0");
    else painter = show(weight);
  }
  return {};
}

/*
  Plastic selection menu: navigate with PREVIOUS and NEXT, cancel with BACK, accept with OK.
*/

HasTimedOut<plastic> Submitter::plasticSelection() {
  painter = scroll("type");
  xTaskNotifyWait(0, -1, nullptr, pdMS_TO_TICKS(2000));
  int i = 0;
  while (true) {
    painter = scroll(plasticName(plastics[i]));
    uint32_t cmd = 0;
    if (!xTaskNotifyWait(-1, -1, &cmd, pdMS_TO_TICKS(idleTimeout))) return {};
    gotInput();
    switch (toAction(cmd)) {
    case Action::PREVIOUS: i = (i + std::size(plastics) - 1) % std::size(plastics); continue;
    case Action::NEXT: i = (i + 1) % std::size(plastics); continue;
    case Action::OK: return plastics[i];
    case Action::BACK: return {};
    }
  }
}

static constexpr const char userAgent[] = "blastic-scale/" BLASTIC_GIT_COMMIT " (" BLASTIC_GIT_WORKTREE_STATUS ")";

/*
  Main submitter logic and UI.
*/

const char *const CSVHeader = "collectionPoint,collectorName,type,epoch,weight";

static const Submitter::FormParameters blasticForm = []() {
  Submitter::FormParameters form;
  form.urn = "docs.google.com/forms/u/0/d/e/1FAIpQLSfmg2pnik2W7wLmmNigjfs4kgNBimxYe5ocRIpuLppBBE35fg/formResponse";
  form.type = "entry.485899545";
  form.collectionPoint = "entry.1447667805";
  form.collectorName = "entry.436217948";
  form.weight = "entry.1288178639";
  return form;
}();

static constexpr const char *const unicodePlasticSymbols[] = {"%E2%99%B3", "%E2%99%B4", "%E2%99%B5", "%E2%99%B6",
                                                              "%E2%99%B7", "%E2%99%B8", "%E2%99%B9"};

void Submitter::loop() [[noreturn]] {
  // display initialization
  matrix.begin();
  matrix.background(0);
  matrix.stroke(0xFFFFFF);
  matrix.textFont(font);
  matrix.beginText(0, 0, 0xFFFFFF);
  auto &LCDinterrupt = matrix.*get(util::ArduinoLEDMatrixBackdoor());
  MSerial()->print("submitter: started lcd\n");
  gotInput();
  auto notice = [this](auto &&msg, int millis = 5000) {
    painter = scroll(std::move(msg));
    return xTaskNotifyWait(0, -1, nullptr, pdMS_TO_TICKS(millis));
  };

  // initial tare on start
  {
    constexpr const uint32_t scaleCliTimeout = 2000, scaleCliMaxMedianWidth = 16;
    auto tare = raw(config.scale, scaleCliMaxMedianWidth, pdMS_TO_TICKS(scaleCliTimeout));
    if (tare == scale::readErr) {
      MSerial()->print("submitter: initial tare failure\n");
      notice("tare fail");
    } else {
      auto &calibration = config.scale.getCalibration();
      calibration.tareRead = tare;
      MSerial serial;
      serial->print("submitter: initial tare ");
      serial->println(tare);
    }
  }

  while (true) {
    if (debug) MSerial()->print("submitter: preview\n");
    auto action = preview();
    if (action.timedOut) {
      if (debug) MSerial()->print("submitter: idling\n");
      LCDinterrupt.stop();
      for (int i = 0; i < matrixHeight * matrixWidth; i++) turnLed(i, false);
      xTimerStop(buttons::measurementTimer(), portMAX_DELAY);
      action = idling();
    }
    LCDinterrupt.start();
    xTimerStart(buttons::measurementTimer(), portMAX_DELAY);
    gotInput();
    if (action != Action::OK) continue;

    // got action OK, start submission
    auto &config = blastic::config.submit;
    if (!std::strlen(config.collectionPoint)) {
      notice("missing collection point name", 10000);
      continue;
    }

    // take median of 10 measurements
    if (debug) MSerial()->print("submitter: start submission\n");
    painter = scroll("...");
    auto weight = scale::weight(blastic::config.scale, 10);
    if (!(weight >= config.threshold)) {
      if (weight < config.threshold) notice("<<1");
      else notice("bad value");
      continue;
    }
    for (int i = 0; i < 5; i++) {
      painter = show(weight);
      if (xTaskNotifyWait(0, -1, nullptr, pdMS_TO_TICKS(200))) break;
      painter = clear();
      if (xTaskNotifyWait(0, -1, nullptr, pdMS_TO_TICKS(200))) break;
    }

    // plastic selection menu
    auto plastic = plasticSelection();
    if (plastic.timedOut) continue;
    painter = scroll(plasticName(plastic), 200, 100, 2);
    xTaskNotifyWait(0, -1, nullptr, pdMS_TO_TICKS(2000));

    // log entry to csv
    const char *SDNotice = nullptr;
    {
      SDCard sd(blastic::config.sdcard.CSPin);
      if (!sd) {
        auto &card = (*sd).*get(util::SDClassBackdoor());
        // if first command timed out, sd is not connected, so skip logging errors
        if (card.errorCode() != SD_CARD_ERROR_CMD0) {
          MSerial()->print("submitter: failed to open SD card to log the measurement\n");
          SDNotice = "SD card error";
        }
        goto SDEnd;
      }
      auto csv = sd->open("data.csv", O_CREAT | O_APPEND | O_WRITE);
      if (!csv) {
        MSerial()->print("submitter: cannot open file data.csv for writing\n");
        SDNotice = "CSV open err";
        goto SDEnd;
      }
      auto epoch = ntp::unixTime();
      if (!epoch) notice("time unset");
      if (!csv.size()) csv.println(CSVHeader);
      csv.print(config.collectionPoint);
      csv.print(',');
      csv.print(config.collectorName);
      csv.print(',');
      csv.print(plasticName(plastic));
      csv.print(',');
      csv.println(epoch);
      csv.print(',');
      csv.println(weight);
      csv.close();
      if (csv.getWriteError()) {
        MSerial serial;
        serial->print("submitter: could not write all data to data.csv\n");
        SDNotice = "CSV write err";
      } else if (debug) MSerial()->print("submitter: entry written successfully to csv\n");
    }
  SDEnd:
    if (SDNotice) notice(SDNotice);

    // send to google form
    using namespace wifi;
    if (!Layer3::firmwareCompatible()) {
      notice("upgrade wifi firmware", 10000);
      continue;
    }
    painter = scroll(blastic::config.submit.skipPPForm ? "user form =>=>=>" : "form =>=>=>");
    {
      Layer3 l3(blastic::config.wifi);
      if (!l3) {
        if (debug) MSerial()->print("submitter: failed to connect to wifi\n");
        notice("wifi error");
        continue;
      }

      enum { UNCONFIGURED, ERROR, OK };
      auto upload = [plastic, weight](const FormParameters &form) {
        const char *path = strchr(form.urn, '/');
        FormParameters::Param serverAddress;
        if (path) serverAddress.strncpy(form.urn, path - form.urn);
        else {
          serverAddress = form.urn;
          path = "/";
        }
        if (!std::strlen(form.urn) || !std::strlen(form.type) || !std::strlen(form.collectionPoint) ||
            !std::strlen(form.weight)) {
          return std::make_tuple(UNCONFIGURED, 0);
        }

        SSLClient tls;
        if (!tls.connect(serverAddress, HttpClient::kHttpsPort)) {
          MSerial serial;
          serial->print("submitter: failed to connect to ");
          serial->println(serverAddress);
          return std::make_tuple(ERROR, 0);
        }

        String formData;
        formData += form.type;
        formData += '=';
        formData += unicodePlasticSymbols[uint8_t(plastic.t) - 1];
        formData += '+'; // space
        formData += plasticName(plastic);
        formData += '&';
        formData += form.collectionPoint;
        formData += '=';
        formData += URLEncoder.encode(blastic::config.submit.collectionPoint);
        formData += '&';
        formData += form.weight;
        formData += '=';
        formData += weight;
        formData += '&';
        formData += form.collectorName;
        formData += '=';
        formData += URLEncoder.encode(
            std::strlen(blastic::config.submit.collectorName) ? blastic::config.submit.collectorName : userAgent);

        auto https = std::make_unique<HttpClient>(tls, serverAddress, HttpClient::kHttpsPort);
        https->beginRequest();
        https->noDefaultRequestHeaders();
        https->connectionKeepAlive();
        https->post(path);
        https->sendHeader("Host", serverAddress);
        https->sendHeader("User-Agent", userAgent);
        https->sendHeader("Content-Type", "application/x-www-form-urlencoded");
        https->sendHeader("Content-Length", formData.length());
        https->sendHeader("Accept", "*/*");
        https->beginBody();
        https->print(formData);
        https->endRequest();

        auto code = https->responseStatusCode();
        if (debug || code != 200) {
          MSerial serial;
          serial->print("submitter: http status ");
          serial->println(code);
        }
        return std::make_tuple(OK, code);
      };

      if (!blastic::config.submit.skipPPForm) {
        auto [state, httpCode] = upload(blasticForm);
        switch (state) {
        case UNCONFIGURED: notice("bad form data"); break;
        case ERROR: notice("connect error"); break;
        case OK:
          if (httpCode == 200) notice("ok!", 2000);
          else {
            std::string errorMsg = "error ";
            errorMsg += httpCode;
            notice(std::move(errorMsg));
          }
        }
      }

      if (strlen(blastic::config.submit.userForm.urn)) {
        painter = scroll("user form =>=>=>");
        auto [state, httpCode] = upload(config.userForm);
        switch (state) {
        case UNCONFIGURED: continue;
        case ERROR: notice("error (user)"); break;
        case OK:
          if (httpCode == 200) notice("ok! (user)", 2000);
          else {
            std::string errorMsg = "error ";
            errorMsg += httpCode;
            errorMsg += " (user)";
            notice(std::move(errorMsg));
          }
        }
      }
    }
  }
}

Submitter::Submitter(const char *name, UBaseType_t priority)
    : painter("Painter", (min(configMAX_PRIORITIES - 1, priority + 1))), task(Submitter::loop, this, name, priority) {
  wifi::Layer3::background() = [](uint32_t &) {
    {
      // probe the wifi to trigger a ntp sync
      wifi::Layer3(blastic::config.wifi);
    }
    return portMAX_DELAY;
  };
}

void Submitter::action(Action action) { xTaskNotify(task, uint8_t(action), eSetValueWithOverwrite); }

void Submitter::action_ISR(Action action) {
  BaseType_t woken = pdFALSE;
  xTaskNotifyFromISR(task, uint8_t(action), eSetValueWithOverwrite, &woken);
  portYIELD_FROM_ISR(woken);
}

} // namespace blastic
