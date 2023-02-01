// This selects between using versioned objects or regular objects
// Versioned objects are implemented as described in:
//   Wei, Ben-David, Blelloch, Fatourou, Rupert and Sun,
//   Constant-Time Snapshots with Applications to Concurrent Data Structures
//   PPoPP 2021
// They support snapshotting via version chains, and without
// indirection, but pointers to objects (ptr_type) must be "recorded
// once" as described in the paper.
#pragma once
#include <parlay/parallel.h>

#ifdef Transactional
// some definitions needed by flock locks (blocking or lock free) when
// using transactions, so must be defined before including flock.
#include <array>
namespace flck {
  template <typename T> struct atomic;
  namespace internal {  struct lock; }}

constexpr int log_len = 100;

template <typename T>
struct transaction_log {
  std::array<T,log_len> vals;
  int cnt;
  void add(T v) {
    if (cnt == log_len) {
      std::cout << "overfull log" << std::endl;
      abort();}
    vals[cnt++] = v;
  }
  void clear() { cnt = 0;}
  transaction_log() : cnt(0) {}
};

namespace verlib { // trans
  struct trans_descriptor;
  struct versioned;
  thread_local trans_descriptor* current_transaction = nullptr;

  thread_local transaction_log<flck::internal::lock*> lock_log;
  thread_local transaction_log<flck::atomic<versioned*>*> write_log;
#ifdef TransUndo
  thread_local transaction_log<bool*> retired_log;
  thread_local transaction_log<bool*> allocated_log;
#endif
}
#endif

#include "flock/flock.h"

#ifdef Versioned

// versioned objects, ptr_type includes version chains
#ifdef Recorded_Once
#include "versioned_recorded_once.h"
#elif FullyIndirect
#include "versioned_indirect.h"
#else
#include "versioned_hybrid.h"
#endif

# else // Not Versioned

namespace verlib {

  struct versioned {};

  template <typename T>
  using versioned_ptr = flck::atomic<T*>;

  template <typename F>
  auto with_snapshot(F f, bool unused_parameter=false) {
    return flck::with_epoch([&] { return f();});
  }
}

#endif

namespace verlib {
  using flck::with_epoch;
  using flck::memory_pool;
}
