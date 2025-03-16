#include <utility>
#include <variant>
#include <memory>
#include "DataFlashBlockDevice.h"
#include "blastic.h"
#include "murmur32.h"

namespace blastic {

namespace eeprom {

// when upgrading the currentVersion, add the relevant code in operator= (as `if constexpr` dependent on versionFrom),
// defaults() and sanitize().

template <uint32_t version>
template <uint32_t versionFrom>
Config<version> &Config<version>::operator=(const Config<versionFrom> &o) {
  scale = o.scale;
  wifi = o.wifi;
  submit = o.submit;
  buttons = o.buttons;
  if constexpr (versionFrom >= 1) sdcard = o.sdcard;
  if constexpr (versionFrom >= 2) ntp.hostname = o.ntp.hostname;
  if constexpr (versionFrom >= 3) ntp.refresh = o.ntp.refresh;
  return *this;
}

template <> void Config<currentVersion>::defaults() {
  memset(this, 0, sizeof(*this));
  header = {.signature = Header::expectedSignature, .Version = currentVersion};
  scale = {.dataPin = 5,
           .clockPin = 4,
           .mode = scale::HX711Mode::A128,
           .calibrations = {// A128 mode by default, calibration parameters that work for me, but not for thee
                            {{.tareRead = 45527, .weightRead = 114810, .weight = 1.56}}}};
  wifi.dhcpTimeout = wifi.idleTimeout = 10;
  submit.threshold = 0.05;
  submit.collectionPoint = "BlastPersis";
  submit.collectorName = "BSPers";
  // OK
  buttons[0] = {
      .pin = 3,
      .threshold = 5234,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 157, .count = 1}};
  // NEXT
  buttons[1] = {
      .pin = 6,
      .threshold = 3698,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 237, .count = 1}};
  // PREVIOUS
  buttons[2] = {
      .pin = 8,
      .threshold = 2967,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 178, .count = 1}};
  // BACK
  buttons[3] = {
      .pin = 9,
      .threshold = 4513,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 186, .count = 1}};
  sdcard.CSPin = 10;
  ntp.hostname = "europe.pool.ntp.org";
  ntp.refresh = 24 * 60 * 60;
};

namespace {

void sanitizeStringBuffers() {}
template <size_t size, typename... Rest>
inline void sanitizeStringBuffers(util::StringBuffer<size> &str, Rest &&...rest) {
  *(str.rbegin()) = '\0';
  sanitizeStringBuffers(std::forward<Rest>(rest)...);
}

} // namespace

template <> void Config<currentVersion>::sanitize() {
  auto defaults = std::make_unique<Config<currentVersion>>();
  defaults->defaults();
  // weak sanitization, just make sure we don't get UB (enums out of range, strings without terminators...)
  if (uint8_t(scale.mode) > uint8_t(scale::HX711Mode::A64)) scale.mode = defaults->scale.mode;
  for (auto &cal : scale.calibrations)
    if (!isfinite(cal.weight)) cal.weight = 0;
  if (!isfinite(submit.threshold) || submit.threshold < 0) submit.threshold = defaults->submit.threshold;
  for (int i = 0; i < size(buttons); i++) {
    auto &defaultButton = defaults->buttons[i], &button = buttons[i];
    if (uint32_t(button.settings.div) > uint32_t(CTSU_CLOCK_DIV_64)) button.settings.div = defaultButton.settings.div;
    if (uint32_t(button.settings.gain) > uint32_t(CTSU_ICO_GAIN_40)) button.settings.gain = defaultButton.settings.gain;
  }
  sanitizeStringBuffers(wifi.ssid, wifi.password, submit.collectionPoint, submit.collectorName,
                        submit.userForm.collectionPoint, submit.userForm.collectorName, submit.userForm.type,
                        submit.userForm.urn, submit.userForm.weight, ntp.hostname);
}

// leave alone the implementation bits below, they do not need to change across Config version updates

namespace {

template <uint32_t... versions> auto makeConfigVariant(std::integer_sequence<uint32_t, versions...>) {
  return std::variant<Config<versions>...>{};
}

using ConfigVariant = decltype(makeConfigVariant(std::make_integer_sequence<uint32_t, currentVersion + 1>{}));

template <uint32_t version = currentVersion>
std::unique_ptr<ConfigVariant> readEepromConfigVariant(uint32_t eepromVersion) {
  if (version == eepromVersion) {
    static_assert(std::is_pod_v<Config<version>>);
    auto variant = std::make_unique<ConfigVariant>();
    auto &config = variant->emplace<Config<version>>();
    auto &flash = DataFlashBlockDevice::getInstance();
    if (flash.read(&config, 0, sizeof(config))) goto error;
    return variant;
  }
  if constexpr (version > 0) return readEepromConfigVariant<version - 1>(eepromVersion);
error:
  return {};
}

template <uint32_t version = currentVersion> constexpr size_t getMaxConfigLength(size_t max = 0) {
  constexpr const size_t size = sizeof(Config<version>);
  size_t newMax = max > size ? max : size;
  if constexpr (!version) return newMax;
  else return getMaxConfigLength<version - 1>(newMax);
}

static_assert(getMaxConfigLength() <= FLASH_TOTAL_SIZE);

} // namespace

const uint32_t maxConfigLength = getMaxConfigLength();

template <uint32_t version> std::tuple<IOret, uint32_t> Config<version>::load() {
  auto ret = std::make_tuple(IOret::ERROR, uint32_t(0));
  auto &flash = DataFlashBlockDevice::getInstance();
  Header eepromHeader;
  if (flash.read(&eepromHeader, 0, sizeof(eepromHeader)) != FSP_SUCCESS) return ret;
  if (eepromHeader.signature != Header::expectedSignature) return std::get<0>(ret) = IOret::NOT_FOUND, ret;
  std::get<1>(ret) = eepromHeader.Version;
  if (eepromHeader.Version > version) return std::get<0>(ret) = IOret::UNKONWN_VERSION, ret;
  auto configVariant = readEepromConfigVariant(eepromHeader.Version);
  if (configVariant->valueless_by_exception()) return ret;
  defaults();
  std::visit([this](auto &&eepromConfig) { *this = eepromConfig; }, *configVariant);
  sanitize();
  return (std::get<0>(ret) = eepromHeader.Version < version ? IOret::UPGRADED : IOret::OK), ret;
}

template <uint32_t version> IOret Config<version>::save() const {
  auto &flash = DataFlashBlockDevice::getInstance();
  return !flash.erase(0, sizeof(*this)) && !flash.program(this, 0, sizeof(*this)) ? IOret::OK : IOret::ERROR;
}

template class Config<currentVersion>;

} // namespace eeprom
} // namespace blastic
