#pragma once

#include "blastic.h"
#include "StaticTask.h"
#include "Looper.h"

namespace blastic {

enum class plastic : uint8_t { PET = 1, HDPE = 2, PVC = 3, LDPE = 4, PP = 5, PS = 6, other = 7 };

constexpr const plastic plastics[]{plastic::PET, plastic::HDPE, plastic::PVC,  plastic::LDPE,
                                   plastic::PP,  plastic::PS,   plastic::other};

constexpr const char *plasticName(plastic p) {
  switch (p) {
  case plastic::PET: return "PET";
  case plastic::HDPE: return "HDPE";
  case plastic::PVC: return "PVC";
  case plastic::LDPE: return "LDPE";
  case plastic::PP: return "PP";
  case plastic::PS: return "PS";
  default: return "other";
  }
}

template <typename T> struct HasTimedOut {

  bool timedOut;
  T t;

  HasTimedOut() : timedOut(true) {}
  HasTimedOut(T &&t) : t(std::move(t)), timedOut(false) {}
  HasTimedOut(const T &t) : t(t), timedOut(false) {}
  operator T &() { return t; }
  operator const T &() const { return t; }
  auto operator->() { return t.operator->(); };
  auto operator->() const { return t.operator->(); };
};

class Submitter {

public:
  // Action is a task notification bit
  enum class Action : uint32_t { NONE = 0, OK = 1, NEXT = 1 << 1, PREVIOUS = 1 << 2, BACK = 1 << 3 };

  struct [[gnu::packed]] EEPROMConfig {
    float threshold;
  };

  Submitter(const char *name, UBaseType_t priority);
  void action(Action action);

protected:
  util::Looper<1024> painter;
  util::StaticTask<4 * 1024> task;
  int lastInteractionMillis;

  void gotInput();
  Action idling();
  HasTimedOut<Action> preview();
  HasTimedOut<plastic> plasticSelection();
  virtual void loop() [[noreturn]];
  static void loop(void *_this) [[noreturn]] { reinterpret_cast<Submitter *>(_this)->loop(); }
};

constexpr Submitter::Action toAction(uint32_t a) {
  /*
  Multiple task notification may be delivered by user input before the notify value read.
  This function normalizes the notification value to discard multiple inputs, as the order
  of execution cannot be detected, and would cause confusion to the user.
  */
  if (a & (a - 1)) return Submitter::Action::NONE;
  return Submitter::Action(a);
}

} // namespace blastic
