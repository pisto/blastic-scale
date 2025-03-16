#include "SerialCliTask.h"
#include "blastic.h"

namespace cli {

using namespace blastic;

namespace {

template <typename T> void valuePrinter(const T &field) {
  MSerial serial;
  serial->print("get: ");
  serial->println(field);
}

void valuePrinter(const scale::HX711Mode &field) {
  MSerial serial;
  serial->print("get: ");
  serial->println(scale::modeStrings[uint32_t(field)]);
}

template <size_t size> void valueParser(WordSplit &args, util::StringBuffer<size> &field, auto &&validate) {
  auto value = args.rest(false, false);
  if (!value) {
    MSerial()->print("set: unspecified value\n");
    return;
  }
set:
  if (!validate(value)) {
    MSerial()->print("set: invalid string\n");
    return;
  }
  field = value;
  MSerial serial;
  serial->print("set: ok '");
  serial->print(field);
  serial->print("'\n");
}

void valueParser(WordSplit &args, scale::HX711Mode &field, auto &&validate) {
  auto value = args.nextWord();
  if (!value) {
    MSerial()->print("set: unspecified value\n");
    return;
  }
  for (int i = 0; i < std::size(scale::modeStrings); i++)
    if (!strcmp(scale::modeStrings[i], value)) {
      auto mode = scale::HX711Mode(i);
      if (!validate(mode)) {
        MSerial()->print("set: invalid mode\n");
        return;
      }
      field = mode;
      MSerial serial;
      serial->print("set: ok ");
      serial->println(value);
      return;
    }
  MSerial()->print("set: cannot parse mode value\n");
}

template <typename T, typename std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
void valueParser(WordSplit &args, T &field, auto &&validate) {
  char *svalue = args.nextWord(), *svalueEnd;
  if (!svalue) {
    MSerial()->print("set: unspecified value\n");
    return;
  }
  auto checkParsed = [svalue, &svalueEnd]() {
    if (svalue == svalueEnd) {
      MSerial()->print("set: cannot parse value\n");
      return false;
    };
    return true;
  };
  auto checkValid = [&validate](auto &value) {
    if (!validate(value)) {
      MSerial()->print("set: invalid value\n");
      return false;
    };
    return true;
  };
  if constexpr (std::is_floating_point_v<T>) {
    auto value = strtof(svalue, &svalueEnd);
    if (!checkParsed() || !checkValid(value)) return;
    field = value;
  } else {
    std::conditional_t<std::is_unsigned_v<T>, uint64_t, int64_t> value;
    if constexpr (std::is_unsigned_v<T>) value = strtoull(svalue, &svalueEnd, 10);
    else value = strtoll(svalue, &svalueEnd, 10);
    if (!checkParsed()) return;
    if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max()) {
      MSerial()->print("set: value is out of range\n");
      return;
    }
    if (!checkValid(value)) return;
    field = value;
  }
  MSerial serial;
  serial->print("set: ok ");
  serial->println(field);
}
void valueParser(WordSplit &args, float &field) {
  valueParser(args, field, [](float v) { return isfinite(v); });
}

template <typename T> void valueParser(WordSplit &args, T &field) {
  valueParser(args, field, [](auto &&) { return true; });
}

#define makeAccessor(address, ...)                                                                                     \
  valueAccessor(                                                                                                       \
      #address, []() { valuePrinter(address); },                                                                       \
      [](WordSplit &args) { return valueParser(args, address, ##__VA_ARGS__); })
#define makeAccessorRO(address) valueAccessor(#address, []() { valuePrinter(address); })

static constexpr const struct valueAccessor {
  using getter = void (*)();
  using setter = void (*)(WordSplit &);
  constexpr valueAccessor() : addressHash(0), get(nullptr), set(nullptr) {}
  constexpr valueAccessor(const char *str, getter get, setter set = nullptr)
      : addressHash(util::murmur3_32(str)), get(get), set(set) {}
  const uint32_t addressHash;
  const getter get;
  const setter set;
} valueAccessors[] = {

    makeAccessorRO(version),
    makeAccessor(debug, [](uint32_t debug) { return debug <= 2; }),
    makeAccessor(wifi::debug, [](uint32_t debug) { return debug <= 3; }),
    makeAccessor(scale::debug::fake),

    makeAccessor(config.scale.mode),
#define makeCalibrationAccessor(prefix, lvalue, field)                                                                 \
  valueAccessor(                                                                                                       \
      prefix "." #field, []() { valuePrinter(lvalue.field); },                                                         \
      [](WordSplit &args) { return valueParser(args, lvalue.field); })
#define makeCalibrationAccessors(prefix, lvalue)                                                                       \
  makeCalibrationAccessor(prefix, lvalue, tareRawRead), makeCalibrationAccessor(prefix, lvalue, weightRawRead),        \
      makeCalibrationAccessor(prefix, lvalue, weight)
    makeCalibrationAccessors("config.scale.calibration", config.scale.getCalibration()),
    makeCalibrationAccessors("config.scale.calibrations.A128",
                             config.scale.calibrations[uint32_t(scale::HX711Mode::A128)]),
    makeCalibrationAccessors("config.scale.calibrations.B", config.scale.calibrations[uint32_t(scale::HX711Mode::B)]),
    makeCalibrationAccessors("config.scale.calibrations.A64",
                             config.scale.calibrations[uint32_t(scale::HX711Mode::A64)]),

    makeAccessor(config.wifi.idleTimeout),
    makeAccessor(config.wifi.ssid),
    makeAccessor(config.wifi.password),
    makeAccessor(config.submit.threshold, [](float v) { return v > 0; }),
    makeAccessor(config.submit.collectionPoint),
    makeAccessor(config.submit.collectorName),
    makeAccessor(config.submit.form.urn),
    makeAccessor(config.ntp.hostname),
    makeAccessor(config.ntp.refresh),
    valueAccessor()};

} // namespace

template <bool get> void accessor(WordSplit &args) {
  auto address = args.nextWord();
  auto prefix = get ? "get: " : "set: ";
  if (!address) {
    MSerial serial;
    serial->print(prefix);
    serial->print("missing address\n");
    return;
  }
  uint32_t addressHash = util::murmur3_32(address);
  for (auto accessor = valueAccessors; accessor->get || accessor->set; accessor++)
    if (accessor->addressHash == addressHash) {
      if (get) {
        if (accessor->get) accessor->get();
        else MSerial()->print("get: address cannot be read\n");
      } else {
        if (accessor->set) accessor->set(args);
        else MSerial()->print("set: address cannot be written\n");
      }
      return;
    }
  MSerial serial;
  serial->print(prefix);
  serial->print("address not found\n");
}

template void accessor<false>(WordSplit &);
template void accessor<true>(WordSplit &);

} // namespace cli
