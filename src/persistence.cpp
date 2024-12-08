#include <utility>
#include <variant>
#include "DataFlashBlockDevice.h"
#include "blastic.h"

namespace blastic {

Config config;

const Config Config::defaults{
    .header = {.signature = Header::expectedSignature, .version = Header::currentVersion},
    .scale = {.dataPin = 5,
              .clockPin = 4,
              .mode = scale::HX711Mode::A128,
              .calibrations = {{.tareRawRead = 45527,
                                .weightRawRead = 114810,
                                .weight = 1.56}, // works for me, but not for thee
                               {.tareRawRead = 0, .weightRawRead = 0, .weight = 0.f},
                               {.tareRawRead = 0, .weightRawRead = 0, .weight = 0.f}}},
    // XXX GCC bug, cannot use initializer lists with strings
    .wifi = WifiConnection::Config{"", "", 10, 10},
    .submit =
        Submitter::Config{
            0.05, "BlastPersis", "BSPers",
            Submitter::Config::FormParameters{
                "docs.google.com/forms/d/e/1FAIpQLSeI3jofIWqtWghblVPOTO1BtUbE8KmoJsGRJuRAu2ceEMIJFw/formResponse",
                "entry.826036805", "entry.458823532", "entry.649832752", "entry.1219969504"}},
    .buttons = {
        {{.pin = 6,
          .threshold = 4749,
          .settings = {
              .div=CTSU_CLOCK_DIV_16, .gain=CTSU_ICO_GAIN_100, .ref_current=0, .offset=309, .count=1}},
         {.pin = 9,
          .threshold = 3387,
          .settings = {
               .div=CTSU_CLOCK_DIV_16, .gain=CTSU_ICO_GAIN_100, .ref_current=0, .offset=226, .count=1}},
         {.pin = 8,
          .threshold = 1166,
          .settings = {
              .div=CTSU_CLOCK_DIV_16, .gain=CTSU_ICO_GAIN_100, .ref_current=0, .offset=169, .count=17}},
         {.pin = 3,
          .threshold = 6310,
          .settings = {
          .div=CTSU_CLOCK_DIV_16, .gain=CTSU_ICO_GAIN_100, .ref_current=0, .offset=185, .count=1}}}}};

namespace eeprom {

static_assert(std::is_pod_v<Config<>>);

template <uint32_t versionTo> Config<versionTo> upgrade(const Config<versionTo - 1> &o);
template <uint32_t versionTo, uint32_t versionFrom> Config<versionTo> upgrade(const Config<versionFrom> &o) {
  static_assert(versionTo >= versionFrom);
  if constexpr (versionFrom == versionTo) return o;
  else return upgrade<versionTo>(upgrade<versionTo - 1>(o));
}

namespace details {

template <uint32_t... versions> auto makeConfigVariant(std::integer_sequence<uint32_t, versions...>) {
  return std::variant<Config<versions>...>{};
}

using ConfigVariant =
    decltype(details::makeConfigVariant(std::make_integer_sequence<uint32_t, Header::currentVersion + 1>{}));

template <uint32_t version = Header::currentVersion> ConfigVariant readConfigVariant(uint32_t eepromVersion) {
  if (version == eepromVersion) {
    ConfigVariant variant = Config<version>();
    auto &flash = DataFlashBlockDevice::getInstance();
    if (flash.read(&std::get<Config<version>>(variant), 0, sizeof(Config<version>))) return {};
    return variant;
  }
  if constexpr (version > 0) return readConfigVariant<version - 1>(eepromVersion);
  else return {};
}

template <typename T, size_t len> inline void sanitizeStrBuffer(T (&str)[len]) {
  static_assert(len > 0);
  str[strnlen(str, len - 1)] = '\0';
}

void sanitizeStrBuffers() {}
template <typename T1, typename... Rest> inline void sanitizeStrBuffers(T1 &&t, Rest &&...r) {
  sanitizeStrBuffer(t);
  sanitizeStrBuffers(std::forward<Rest>(r)...);
}

template <uint32_t version = Header::currentVersion> constexpr size_t maxConfigLength(size_t max = 0) {
  size_t size = sizeof(Config<version>);
  if constexpr (!version) return max > size ? max : size;
  else return maxConfigLength<version - 1>(max > size ? max : size);
}

} // namespace details

const uint32_t maxConfigLength = details::maxConfigLength();

IOret Config<>::load() {
  auto &flash = DataFlashBlockDevice::getInstance();
  if (flash.read(&header, 0, sizeof(header)) != FSP_SUCCESS) return IOret::ERROR;
  if (header.signature != header.expectedSignature) return IOret::NOT_FOUND;
  if (header.version > Header::currentVersion) return IOret::UNKONWN_VERSION;
  auto configVariant = details::readConfigVariant(header.version);
  if (configVariant.valueless_by_exception()) return IOret::ERROR;
  *this = std::visit([](auto &&configOldVersion) { return upgrade<Header::currentVersion>(configOldVersion); }, configVariant);
  sanitize();
  return header.version == Header::currentVersion ? IOret::OK : IOret::UPGRADED;
}

void Config<>::sanitize() {
  // weak sanitization, just make sure we don't get UB (enums out of range, strings without terminators...)
  auto &def = Config::defaults;
  if (uint8_t(scale.mode) > uint8_t(scale::HX711Mode::A64)) scale.mode = def.scale.mode;
  for (auto &cal : scale.calibrations)
    if (!isfinite(cal.weight)) cal.weight = 0;
  if (!isfinite(submit.threshold) || submit.threshold < 0) submit.threshold = def.submit.threshold;
  for (auto &button : buttons) {
    if (uint32_t(button.settings.div) > uint32_t(CTSU_CLOCK_DIV_64)) button.settings.div = def.buttons[0].settings.div;
    if (uint32_t(button.settings.gain) > uint32_t(CTSU_ICO_GAIN_40))
      button.settings.gain = def.buttons[0].settings.gain;
  }
  details::sanitizeStrBuffers(wifi.ssid, wifi.password, submit.collectionPoint, submit.collectionPoint,
                              submit.collectorName, submit.form.collectionPoint, submit.form.collectorName,
                              submit.form.type, submit.form.urn, submit.form.weight);
}

IOret Config<>::save() const {
  static_assert(sizeof(*this) <= FLASH_TOTAL_SIZE);
  auto &flash = DataFlashBlockDevice::getInstance();
  return !flash.erase(0, sizeof(*this)) && !flash.program(this, 0, sizeof(*this)) ? IOret::OK : IOret::ERROR;
}

} // namespace eeprom
} // namespace blastic
