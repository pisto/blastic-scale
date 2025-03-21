#pragma once

#include <Arduino.h>
#include <Arduino_FreeRTOS.h>

namespace util {

/*
  Utility class to automatically create a mutex for any global object
  that comes from the platform framework, and implements cooperative
  locking for it. Use as this, for example with Serial:

  { // mutex is acquired here
    util::Mutexed<Serial> lockedSerial;
    lockedSerial->print("This write");
    lockedSerial->print(" won't be");
    lockedSerial->print(" interleaved with");
    lockedSerial->println(" other tasks' writes!");
  } // mutex is released here

  // note that direct access to the Serial object here cannot be prevented
  Serial->println("You may see pieces of this string interleaved with other tasks' output!");


  The class uses one static member per template parameter obj. It should be
  initialized in a single thread context (such as in void setup()) by
  acquiring it once.
*/

template <auto &obj> class Mutexed {
  inline static StaticSemaphore_t buffer;
  inline static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutexStatic(&buffer);
  template <typename T> friend class MutexedGenerator;

public:
  Mutexed() { configASSERT(xSemaphoreTakeRecursive(Mutexed::mutex, portMAX_DELAY)); }
  Mutexed(const Mutexed &) = delete;
  Mutexed &operator=(const Mutexed &) = delete;
  ~Mutexed() { configASSERT(xSemaphoreGiveRecursive(Mutexed::mutex)); }

  auto operator->() const { return &obj; }
  auto &operator*() const { return obj; }
};

/*
  This class implements template value erasure for the Mutexed class: if you want to write a non-template function
  that makes use of a Mutexed, first create a MutexedGenerator and pass it to your function. The generator returns
  a MutexedDynamic object that has the same interface of a Mutexed object.
*/

template <typename T> class MutexedGenerator {
  T &obj;
  SemaphoreHandle_t &mutex;

  MutexedGenerator(T &obj, SemaphoreHandle_t &mutex) : obj(obj), mutex(mutex) {}

public:
  template <auto &obj> static MutexedGenerator get() { return MutexedGenerator(obj, Mutexed<obj>::mutex); }

  class MutexedDynamic {
    const MutexedGenerator &generator;
    MutexedDynamic(const MutexedGenerator &generator) : generator(generator) {
      configASSERT(xSemaphoreTakeRecursive(generator.mutex, portMAX_DELAY));
    }
    friend class MutexedGenerator;

  public:
    MutexedDynamic() = delete;
    MutexedDynamic(const MutexedDynamic &) = delete;
    MutexedDynamic &operator=(const MutexedDynamic &) = delete;
    ~MutexedDynamic() { configASSERT(xSemaphoreGiveRecursive(generator.mutex)); }

    T *operator->() const { return &generator.obj; }
    T &operator*() const { return generator.obj; }
  };

  MutexedDynamic lock() const { return MutexedDynamic(*this); }
};

} // namespace util
