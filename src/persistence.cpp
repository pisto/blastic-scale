#include <utility>
#include <variant>
#include "DataFlashBlockDevice.h"
#include "blastic.h"
#include "murmur32.h"

namespace blastic {

namespace eeprom {

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
  // this function is required to exist by std::visit() on a variant of Config<versions>..., but should not be called
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
    Config<version> config;
    static_assert(std::is_pod_v<Config<version>>);
    auto &flash = DataFlashBlockDevice::getInstance();
    if (flash.read(&config, 0, sizeof(config))) return {};
    return config;
  }
  if constexpr (version > 0) return readConfigVariant<version - 1>(eepromVersion);
  else return {};
}

void sanitizeStringBuffers() {}
template <size_t size, typename... Rest>
inline void sanitizeStringBuffers(util::StringBuffer<size> &str, Rest &&...rest) {
  *(str.rbegin()) = '\0';
  sanitizeStringBuffers(std::forward<Rest>(rest)...);
}

template <uint32_t version = Header::currentVersion> constexpr size_t getMaxConfigLength(size_t max = 0) {
  size_t size = sizeof(Config<version>);
  if constexpr (!version) return max > size ? max : size;
  else return maxConfigLength<version - 1>(max > size ? max : size);
}

static_assert(getMaxConfigLength() <= FLASH_TOTAL_SIZE);

} // namespace

const uint32_t maxConfigLength = getMaxConfigLength();

void Config<>::defaults() {
  memset(this, 0, sizeof(*this));
  header = {.signature = eeprom::Header::expectedSignature, .version = eeprom::Header::currentVersion};
  scale = {.dataPin = 5,
           .clockPin = 4,
           .mode = scale::HX711Mode::A128,
           .calibrations = {// A128 mode by default, calibration parameters that work for me, but not for thee
                            {.tareRawRead = 45527, .weightRawRead = 114810, .weight = 1.56}}};
  wifi.dhcpTimeout = wifi.disconnectTimeout = 10;
  submit.threshold = 0.05;
  submit.collectionPoint = "BlastPersis";
  submit.collectorName = "BSPers";
  submit.form.urn = "docs.google.com/forms/d/e/1FAIpQLSeI3jofIWqtWghblVPOTO1BtUbE8KmoJsGRJuRAu2ceEMIJFw/formResponse";
  submit.form.type = "entry.826036805";
  submit.form.collectionPoint = "entry.458823532";
  submit.form.collectorName = "entry.649832752";
  submit.form.weight = "entry.1219969504";
  // OK
  buttons[0] = {
      .pin = 3,
      .threshold = 5234,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 157, .count = 1}};
  // NEXT
  buttons[1] = {
      .pin = 8,
      .threshold = 3698,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 178, .count = 1}};
  // PREVIOUS
  buttons[2] = {
      .pin = 6,
      .threshold = 3698,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 237, .count = 1}};
  // BACK
  buttons[3] = {
      .pin = 9,
      .threshold = 4513,
      .settings = {.div = CTSU_CLOCK_DIV_18, .gain = CTSU_ICO_GAIN_100, .ref_current = 0, .offset = 186, .count = 1}};
};

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
  *this = std::get<eeprom::Config<>>(configVariant);
  header = {.signature = Header::expectedSignature, .version = Header::currentVersion};
  return eepromHeader.version < header.version ? IOret::UPGRADED : IOret::OK;
}

bool Config<>::sanitize() {
  Config d;
  d.defaults();
  auto hashPreSanitize = util::murmur3_32(reinterpret_cast<const unsigned char*>(this), sizeof(*this));
  // weak sanitization, just make sure we don't get UB (enums out of range, strings without terminators...)
  if (uint8_t(scale.mode) > uint8_t(scale::HX711Mode::A64)) scale.mode = d.scale.mode;
  for (auto &cal : scale.calibrations)
    if (!isfinite(cal.weight)) cal.weight = 0;
  if (!isfinite(submit.threshold) || submit.threshold < 0) submit.threshold = d.submit.threshold;
  for (int i = 0; i < size(buttons); i++) {
    auto &defaultButton = d.buttons[i], &button = buttons[i];
    if (uint32_t(button.settings.div) > uint32_t(CTSU_CLOCK_DIV_64)) button.settings.div = defaultButton.settings.div;
    if (uint32_t(button.settings.gain) > uint32_t(CTSU_ICO_GAIN_40)) button.settings.gain = defaultButton.settings.gain;
  }
  sanitizeStringBuffers(wifi.ssid, wifi.password, submit.collectionPoint, submit.collectionPoint, submit.collectorName,
                        submit.form.collectionPoint, submit.form.collectorName, submit.form.type, submit.form.urn,
                        submit.form.weight);
  return hashPreSanitize == util::murmur3_32(reinterpret_cast<const unsigned char*>(this), sizeof(*this));
}

IOret Config<>::save() const {
  auto &flash = DataFlashBlockDevice::getInstance();
  return !flash.erase(0, sizeof(*this)) && !flash.program(this, 0, sizeof(*this)) ? IOret::OK : IOret::ERROR;
}

} // namespace eeprom
} // namespace blastic
