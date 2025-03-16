#include <cm_backtrace/cm_backtrace.h>
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include "StaticTask.h"
#include "Mutexed.h"
#include "utils.h"

#if configSUPPORT_STATIC_ALLOCATION == 1

/*
  Static allocation functions, as suggested in the FreeRTOS guide.
*/

#if configUSE_TIMERS == 1

void vApplicationGetTimerTaskMemory(StaticTask_t **taskBuffer, StackType_t **stackBuffer, uint32_t *stackSize) {
  static util::StaticTask timerTaskBuffers;
  *taskBuffer = &timerTaskBuffers.taskBuffer;
  *stackBuffer = timerTaskBuffers.stack;
  *stackSize = sizeof(timerTaskBuffers.stack) / sizeof(timerTaskBuffers.stack[0]);
}

#endif

void vApplicationGetIdleTaskMemory(StaticTask_t **taskBuffer, StackType_t **stackBuffer, uint32_t *stackSize) {
  static util::StaticTask idleTaskBuffers;
  *taskBuffer = &idleTaskBuffers.taskBuffer;
  *stackBuffer = idleTaskBuffers.stack;
  *stackSize = sizeof(idleTaskBuffers.stack) / sizeof(idleTaskBuffers.stack[0]);
}

#endif

#if configCHECK_FOR_STACK_OVERFLOW > 0

/*
  FreeRTOS guide says "here is no real way to recover from a stack overflow
  when it occurs". Here we trigger an hardware fault (call to invalid address)
  in order to trigger a crash dump on Serial. The fault log is provided by
  https://github.com/armink/CmBacktrace , which eventually prints an addr2line
  command to show the stack trace.

  The function is naked to avoid messing with the stack in an already broken
  situation.
*/
[[gnu::naked]] void vApplicationStackOverflowHook(TaskHandle_t, char *) { reinterpret_cast<voidFuncPtr>(0)(); }

#endif

/*
  The Arduino Framework makes use (in some questionable places) of malloc/free,
  new/delete. Unfortunately, the C library (and libfsp, which provides heap
  space in a .heap section) are precompiled and cannot be modified. Use the
  -Wl--wrapper linker trick to make these calls safe under FreeRTOS.

  Arduino Framework uses picolib, which makes malloc and friends safe by using
  a lock (compiled to a no-op, since Arduino assumes it is single-thread). Here
  hook _malloc_lock/__malloc_unlock to vTaskSuspendAll/xTaskResumeAll. This
  ensures that all access to the heap (malloc/realloc/calloc...) is safe.
*/
extern "C" void __real___malloc_lock(_reent *);
extern "C" void __wrap___malloc_lock(_reent *) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) vTaskSuspendAll();
}

extern "C" void __real___malloc_unlock(_reent *);
extern "C" void __wrap___malloc_unlock(_reent *) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) xTaskResumeAll();
}

#if configUSE_MALLOC_FAILED_HOOK

/*
  Trace all failed allocations.
*/

void vApplicationMallocFailedHook() { configASSERT(false && "pvPortMalloc() failed"); }

extern "C" void *__real__malloc_r(struct _reent *, size_t);
extern "C" void *__wrap__malloc_r(struct _reent *r, size_t s) {
  auto ptr = __real__malloc_r(r, s);
  configASSERT(ptr && "_malloc_r() failed");
  return ptr;
}

#endif

namespace util {

void printStackTrace(const StackTrace &trace, size_t depth, Print &print) {
  for (int i = 0; i < depth; i++) {
    if (i) print.print(' ');
    print.print(trace[i], 16);
  }
}

} // namespace util

/*
  Hook failed assert to a Serial print, then throw a stack trace every 10 seconds.
*/

constexpr const int assertSleepMillis = 10000;

static void _assert_func_freertos(const char *file, int line, const char *failedExpression,
                                  const util::StackTrace &trace, size_t depth) [[noreturn]] {
  vTaskPrioritySet(nullptr, tskIDLE_PRIORITY + 1);
  while (true) {
    {
      util::Mutexed<::Serial> serial;
      if (!*serial) serial->begin(BLASTIC_MONITOR_SPEED);
      while (!*serial);
      serial->print("assert: ");
      serial->print(file);
      serial->print(':');
      serial->print(line);
      serial->print(" failed expression ");
      serial->println(failedExpression);
      serial->print("assert: addr2line -e $FIRMWARE_FILE -a -f -C ");
      util::printStackTrace(trace, depth, *serial);
      serial->println();
    }
    vTaskDelay(pdMS_TO_TICKS(assertSleepMillis));
  }
}

static void _assert_func_arduino(const char *file, int line, const char *failedExpression,
                                 const util::StackTrace &trace, size_t depth) [[noreturn]] {
  if (!Serial) Serial.begin(BLASTIC_MONITOR_SPEED);
  while (!Serial);
  while (true) {
    Serial.print("assert: ");
    Serial.print(file);
    Serial.print(':');
    Serial.print(line);
    Serial.print(" failed expression ");
    Serial.println(failedExpression);
    Serial.print("assert: addr2line -e $FIRMWARE_FILE -a -f -C ");
    util::printStackTrace(trace, depth, Serial);
    Serial.println();
    delay(assertSleepMillis);
  }
}

// this is a weak function in the Arduino framework so we just need to declare it here without the -Wl,--wrap trick
extern "C" void __assert_func(const char *file, int line, const char *, const char *failedExpression) [[noreturn]] {
  util::StackTrace trace;
  auto stackDepth = util::stackTrace(trace);
  (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING ? _assert_func_freertos : _assert_func_arduino)(
      file, line, failedExpression, trace, stackDepth);
}

void loop() [[noreturn]] {
  Serial.print("loop: starting FreeRTOS scheduler\n");
  vTaskStartScheduler();
  assert(false && "vTaskStartScheduler() should never return");
}
