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
            0.05, "", "",
            Submitter::Config::FormParameters{
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

namespace eeprom {

static_assert(std::is_pod_v<Config<>>);
static_assert(sizeof(Config<>) <= FLASH_TOTAL_SIZE);

/*

  In order to support the upgrade of an older config to the actual version, place here specializations of template
  struct Config to record the older formats:

template <> struct Config<someVersion> {
  Header header;
  // old configuration fields [...]
};

  Then place a specialization of template function upgradeOnce, that takes a Config<someVersion>
  reference and returns a Config<someVersion + 1> object, with a reasonable conversion logic.

*/

template <uint32_t version> auto upgradeOnce(const Config<version> &o) {
  // test must be always false, but got to reference a template parameter (rather than using false) otherwise the
  // compiler will actually compile this function and fail
  static_assert(!(version ^ Header::currentVersion), "Missing specialization for a Config<version>");
  return o;
}
template <> auto upgradeOnce(const Config<Header::currentVersion> &o) {
  // this function is required to exist by std::visit() on a variant, but should not be called
  assert(false && "Config upgradeOnce function called on the current version");
  return o;
}

namespace {

template <uint32_t... versions> auto makeConfigVariant(std::integer_sequence<uint32_t, versions...>) {
  return std::variant<Config<versions>...>{};
}

using ConfigVariant = decltype(makeConfigVariant(std::make_integer_sequence<uint32_t, Header::currentVersion + 1>{}));

template <uint32_t version> constexpr uint32_t getVersion(const Config<version> &c) { return version; }

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

template <uint32_t version = Header::currentVersion> constexpr size_t getMaxConfigLength(size_t max = 0) {
  size_t size = sizeof(Config<version>);
  if constexpr (!version) return max > size ? max : size;
  else return maxConfigLength<version - 1>(max > size ? max : size);
}

} // namespace

const uint32_t maxConfigLength = getMaxConfigLength();

IOret Config<>::load() {
  auto &flash = DataFlashBlockDevice::getInstance();
  Header eepromHeader;
  if (flash.read(&eepromHeader, 0, sizeof(eepromHeader)) != FSP_SUCCESS) return IOret::ERROR;
  if (eepromHeader.signature != Header::expectedSignature) return IOret::NOT_FOUND;
  if (eepromHeader.version > Header::currentVersion) return IOret::UNKONWN_VERSION;
  auto configVariant = readConfigVariant(eepromHeader.version);
  if (configVariant.valueless_by_exception()) return IOret::ERROR;
  while (configVariant.index() < std::variant_size_v<ConfigVariant> - 1)
    configVariant = std::visit([](auto &&configOldVersion) { return upgradeOnce(configOldVersion); }, configVariant);
  auto &loaded = std::get<Config<>>(configVariant);
  loaded.sanitize();
  *this = loaded;
  return eepromHeader.version == Header::currentVersion ? IOret::OK : IOret::UPGRADED;
}

void Config<>::sanitize() {
  header.signature = Header::expectedSignature;
  header.version = Header::currentVersion;
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
  sanitizeStrBuffers(wifi.ssid, wifi.password, submit.collectionPoint, submit.collectionPoint, submit.collectorName,
                     submit.form.collectionPoint, submit.form.collectorName, submit.form.type, submit.form.urn,
                     submit.form.weight);
}

IOret Config<>::save() const {
  auto &flash = DataFlashBlockDevice::getInstance();
  return !flash.erase(0, sizeof(*this)) && !flash.program(this, 0, sizeof(*this)) ? IOret::OK : IOret::ERROR;
}

} // namespace eeprom
} // namespace blastic
