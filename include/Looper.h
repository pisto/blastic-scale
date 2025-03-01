#pragma once

#include <functional>
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include "StaticTask.h"

namespace util {

namespace details {

void looperLoop(QueueHandle_t queue) [[noreturn]];
void looperTerminate(QueueHandle_t queue, TaskHandle_t looperTask);

} // namespace details

using loopFunction = std::function<TickType_t(uint32_t &)>;

/*
  This class implements a task that continuously runs a provided loopFunction. Think of a thread pool with a single
  thread. The function can request delays, can cancel itself, and the object owner can change the function at any time.
  The function is guaranteed to be called at least once.

  The loopFunction is a std::function that accepts a counter argument (which can be modified by the function itself),
  and returns the ticks that the Looper should wait before calling it again. Return portMAX_DELAY to stop calling the
  function and to release its closure.
*/

template <size_t StackSize = configMINIMAL_STACK_SIZE * sizeof(StackType_t)> class Looper {

public:
  Looper(const char *name, UBaseType_t priority)
      : queue(xQueueCreateStatic(1, sizeof(loopFunction *), queueObjectsBuff, &queueBuff)),
        task(Looper::loop, this, name, priority) {}

  Looper(const Looper &) = delete;
  Looper &operator=(const Looper &) = delete;
  Looper &operator=(loopFunction &&loop) {
    auto loopPtr = loop ? new loopFunction(std::move(loop)) : nullptr;
    configASSERT(xQueueSend(queue, &loopPtr, portMAX_DELAY));
    return *this;
  }

  bool set(loopFunction &&loop, TickType_t delay = portMAX_DELAY) {
    auto loopPtr = loop ? new loopFunction(std::move(loop)) : nullptr;
    if (xQueueSend(queue, &loopPtr, delay)) return true;
    delete loopPtr;
    return false;
  }

  operator TaskHandle_t() const { return task; }

  ~Looper() { details::looperTerminate(queue, task); }

private:
  StaticQueue_t queueBuff;
  uint8_t queueObjectsBuff[sizeof(loopFunction *)];
  const QueueHandle_t queue;
  StaticTask<StackSize> task;

  static void loop(void *_this) [[noreturn]] { details::looperLoop(reinterpret_cast<Looper *>(_this)->queue); }
};

} // namespace util
