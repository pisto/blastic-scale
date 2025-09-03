#pragma once

#include <Arduino.h>
#include <Arduino_FreeRTOS.h>

namespace util {

/*
  Utility class to allocate static buffers for FreeRTOS tasks. Normally
  used as static function variables.

  The stack size can be configured in bytes (not StackType_t which is
  sizeof(StackType_t) == 4 usually!), defaulting to the minimal FreeRTOS
  stack size.
*/

template <size_t StackSize = configMINIMAL_STACK_SIZE * sizeof(StackType_t)> class StaticTask {

public:
  static_assert(StackSize >= sizeof(StackType_t) * configMINIMAL_STACK_SIZE);

  StaticTask_t taskBuffer;
  StackType_t stack[(StackSize + sizeof(StackType_t) - 1) / sizeof(StackType_t)];

  StaticTask() = default;

  template <typename ArgType>
  StaticTask(TaskFunction_t taskFunction, ArgType arg, const char *name, UBaseType_t priority = tskIDLE_PRIORITY + 1) {
    init(taskFunction, arg, name, priority);
  }

  StaticTask(voidFuncPtr taskFunction, const char *name, UBaseType_t priority = tskIDLE_PRIORITY + 1) {
    init(taskFunction, name, priority);
  }

  template <typename ArgType>
  TaskHandle_t init(TaskFunction_t taskFunction, ArgType arg, const char *name,
                    UBaseType_t priority = tskIDLE_PRIORITY + 1) {
    static_assert(sizeof(ArgType) == sizeof(void *));
    configASSERT(!handle);
    return handle = xTaskCreateStatic(taskFunction, name, sizeof(stack) / sizeof(stack[0]), static_cast<void *>(arg),
                                      priority, stack, &taskBuffer);
  }

  TaskHandle_t init(voidFuncPtr taskFunction, const char *name, UBaseType_t priority = tskIDLE_PRIORITY + 1) {
    static_assert(sizeof(TaskFunction_t) == sizeof(voidFuncPtr));
    return init(reinterpret_cast<TaskFunction_t>(taskFunction), nullptr, name, priority);
  }

  StaticTask(const StaticTask &) = delete;
  StaticTask &operator=(const StaticTask &) = delete;

  operator TaskHandle_t() const { return handle; }
  operator bool() const { return handle; }

  ~StaticTask() {
    if (handle) vTaskDelete(handle);
  }

private:
  TaskHandle_t handle = nullptr;
};

} // namespace util
