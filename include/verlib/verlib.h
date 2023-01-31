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
#define Trans_Undo 1
#include <array>
namespace flck { namespace internal {  struct lock; }}
namespace verlib { // trans
  struct trans_descriptor;
  thread_local trans_descriptor* current_transaction = nullptr;

  constexpr int lock_log_len = 100;
  thread_local std::array<flck::internal::lock*,lock_log_len> lock_log;
  thread_local int lock_log_cnt = 0;

  constexpr int retired_log_len = 100;
  thread_local std::array<bool*,retired_log_len> retired_log;
  thread_local int retired_log_cnt = 0;

  constexpr int allocated_log_len = 100;
  thread_local std::array<bool*,allocated_log_len> allocated_log;
  thread_local int allocated_log_cnt = 0;
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
