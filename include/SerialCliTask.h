#pragma once

#include <algorithm>
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include "Mutexed.h"
#include "StaticTask.h"
#include "murmur32.h"

namespace cli {

class WordSplit;
class CliCallback;

namespace details {

struct SerialCliTaskState {
  const CliCallback *const callbacks;
};
void consumeAutostartFiles(const SerialCliTaskState &_this, util::MutexedGenerator<Print> outputMutexGen);
void loop(const SerialCliTaskState &_this, Stream &input, util::MutexedGenerator<Print> outputMutexGen, bool loop);

} // namespace details

/*
  A CliCallback struct contains the MurMur3 hash of the command string, and
  the corresponding function pointer to call.

  The constructors are all constexpr, so that the compiler can avoid emitting
  the string for the command.
*/

class CliCallback {

public:
  using CliFunctionPointer = void (*)(WordSplit &args);

  constexpr CliCallback() : cliCommandHash(0), function(nullptr) {}

  constexpr CliCallback(const char *str, CliFunctionPointer function)
      : cliCommandHash(util::murmur3_32(str)), function(function) {}

private:
  friend void details::loop(const details::SerialCliTaskState &_this, Stream &input,
                            util::MutexedGenerator<Print> outputMutexGen, bool loop);

  const uint32_t cliCommandHash;
  const CliFunctionPointer function;

  template <auto &, size_t> friend class SerialCliTask;
};

/*
  Use makeCliCallback(ns::func) as CliCallback("ns::func", ns::func)
*/
#define makeCliCallback(func) CliCallback(#func, func)

/*
  Utility class to tokenize a string by spaces.
  Each token returned by nextWord() is a pointer to the buffer
  passed in the constructor, trimmed left and right of spaces.
  The buffer is modified in-place with '\0' terminators, so
  it is not possible to reuse WordSplit on the same pointer.
*/

class WordSplit {
public:
  char *str;

  WordSplit(char *str) : str(str) {}

  char *nextWord() {
    while (*str && isspace(*str)) str++;
    if (!*str) return nullptr;
    auto result = str++;
    while (*str && !isspace(*str)) str++;
    if (*str) *str++ = '\0';
    return result;
  }

  char *rest(bool ltrim = true, bool rtrim = true) {
    while (ltrim && *str && isspace(*str)) str++;
    auto result = str;
    str += strlen(str);
    while (rtrim && str > result && isspace(str[-1])) *--str = '\0';
    return !*result ? nullptr : result;
  }

  bool nextWordIs(const char *str) {
    auto word = nextWord();
    return word && !strcmp(word, str);
  }
};

/*
  Simple implementation of a command line interface over Serial.

  The SerialCliTask implements a FreeRTOS task that reads from serial and
  parses command in the form:

    <command> [args...]

  The list of commands is provided by the user as an array of CliCallback structures,
  matching command strings to function pointers. This array can be created as a
  constexpr expression, to avoid emitting the command strings in the binary:

  static constexpr const CliCallback callbacks[]{
    CliCallback("func1", ::func1),
    CliCallback("func2", tools::func2),
    CliCallback()
  };

  The terminating empty CliCallback() is necessary.

  This CLI task runs with the highest priority available, in order to be able to work
  even when other tasks hang for any reason.

  This class is a template in order to use util::Mutexed<serial>. Note however that
  mutexed access is used only when printing error messages from the callback parsing function.
  Access in read to the serial does *not* use locking, as we assume that we are the only
  client reading serial.

  This class also changes the timeout of the Serial device, in order to avoid blocking
  reads, which would slow down the device as this task runs with the maximum priority.
  This is done to enable the execution of debug command via serial in a timely manner,
  regardless of the other tasks' state.
*/

template <auto &serial, size_t StackSize = configMINIMAL_STACK_SIZE * sizeof(StackType_t)> class SerialCliTask {

public:
  using MSerial = util::Mutexed<serial>;

  // Note: the serial must be initialized alreay
  SerialCliTask(const CliCallback *callbacks, const char *name = "SerialCliTask",
                UBaseType_t priority = configMAX_PRIORITIES - 1)
      : _this({callbacks}), task(SerialCliTask::loop, this, name, priority) {
    /*
      Read operations busy poll using millis(). This task
      runs with the maximum priority, so we must not starve the other
      tasks in blocking read operations. Polling with task delays is
      handled in loop().
    */
    serial.setTimeout(0);
  }

private:
  details::SerialCliTaskState _this;
  util::StaticTask<StackSize> task;
  static void loop(void *me) [[noreturn]] {
    auto &_this = *reinterpret_cast<SerialCliTask *>(me);
    details::consumeAutostartFiles(_this._this, util::MutexedGenerator<Print>::get<serial>());
    details::loop(_this._this, serial, util::MutexedGenerator<Print>::get<serial>(), true);
  }
};

} // namespace cli
