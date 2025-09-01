#include "blastic.h"
#include "SerialCliTask.h"

namespace cli {

namespace details {

// task delay in poll loop, milliseconds
static constexpr const uint32_t pollInterval = 250;

void consumeAutostartFiles(const SerialCliTaskState &_this, util::MutexedGenerator<Print> outputMutexGen) {
  using namespace blastic;
  SDCard sd(config.sdcard.CSPin);
  if (!sd) {
    outputMutexGen.lock()->print("cli: cannot initialize SD card to read autostart files\n");
    return;
  }
  auto autostart = sd->open("cmdboot");
  if (autostart) {
    outputMutexGen.lock()->print("cli: found cmdboot file, now executing commands\n");
    loop(_this, autostart, outputMutexGen, false);
    autostart.close();
  }
  auto autostartOnce = sd->open("cmdonce");
  if (autostartOnce) {
    outputMutexGen.lock()->print("cli: found cmdonce file, now executing commands then removing the file\n");
    loop(_this, autostartOnce, outputMutexGen, false);
    autostartOnce.close();
    sd->remove("cmdonce");
  }
}

void loop(const SerialCliTaskState &_this, Stream &input, util::MutexedGenerator<Print> outputMutexGen, bool loop) {
  /*
    Avoid String usage at all costs because it uses realloc(), shoot in your foot with pointer arithmetic.

    This function reads input into a static buffer, and parses a command line per '\n'-terminated line of input.

    Command names are trimmed by WordSplit.
  */
  constexpr const size_t maxLen = std::min(255, SERIAL_BUFFER_SIZE - 1);
  char inputBuffer[maxLen + 1];
  size_t len = 0;
  // polling loop
  bool keepreading = true;
  while (keepreading) {
    auto oldLen = len;
    // this is non blocking with the serial interface as we used setTimeout(0) on initialization
    len += input.readBytes(inputBuffer + len, maxLen - len);
    if (oldLen == len) {
      if (!loop) keepreading = false;
      else {
        vTaskDelay(pdMS_TO_TICKS(pollInterval));
        continue;
      }
    }
    // input may contain the null character, sanitize to a newline
    for (auto c = inputBuffer + oldLen; c < inputBuffer + len; c++) *c = *c ?: '\n';
    // make sure this is always a valid C string
    inputBuffer[len] = '\0';
    // parse loop
    while (true) {
      auto lineEnd = strchr(inputBuffer, '\n');
      if (!lineEnd) {
        if (!loop) lineEnd = inputBuffer + len;
        else {
          if (len == maxLen) {
            outputMutexGen.lock()->print("cli: buffer overflow while reading input\n");
            // discard buffer
            len = 0;
          }
          break;
        }
      }
      // truncate the command line C string at '\n'
      *lineEnd = '\0';
      // interpret \b as backspace and ignore \r
      auto dst = inputBuffer;
      for (auto src = inputBuffer; src < lineEnd; src++) {
        if (*src == '\r') continue;
        if (*src != '\b') *dst++ = *src;
        else if (dst > inputBuffer) --dst;
      }
      *dst = '\0';
      WordSplit commandLine(inputBuffer);
      auto command = commandLine.nextWord();
      uint32_t commandHash;
      // skip if line is empty
      if (!command || !*command) goto shiftLeftBuffer;
      commandHash = util::murmur3_32(command);
      for (auto callback = _this.callbacks; callback->function; callback++)
        if (callback->cliCommandHash == commandHash) {
          callback->function(commandLine);
          goto shiftLeftBuffer;
        }
      {
        auto output = outputMutexGen.lock();
        output->print("cli: command not found: ");
        output->println(command);
      }
      // move bytes after newline to start of buffer, repeat
    shiftLeftBuffer:
      auto nextLine = lineEnd + 1;
      if (nextLine - inputBuffer >= len) {
        len = 0;
        break;
      }
      auto leftoverLen = len - (nextLine - inputBuffer);
      memmove(inputBuffer, nextLine, leftoverLen);
      len = leftoverLen;
    }
  }
}

} // namespace details

} // namespace cli
