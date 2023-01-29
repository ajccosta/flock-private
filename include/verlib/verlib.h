// This selects between using versioned objects or regular objects
// Versioned objects are implemented as described in:
//   Wei, Ben-David, Blelloch, Fatourou, Rupert and Sun,
//   Constant-Time Snapshots with Applications to Concurrent Data Structures
//   PPoPP 2021
// They support snapshotting via version chains, and without
// indirection, but pointers to objects (ptr_type) must be "recorded
// once" as described in the paper.
#pragma once
#include <array>
namespace flck {
  namespace internal {
    struct lock;
  }
}

namespace verlib { // trans
  struct versioned;
  thread_local std::array<flck::internal::lock*,100> lock_log;
  thread_local int lock_log_len;
  thread_local std::array<versioned*,100> write_log;
  thread_local int write_log_len;
  thread_local bool in_transaction;
}

#include "flock/flock.h"

#ifdef Versioned

// versioned objects, ptr_type includes version chains
#ifdef Transactional
#include "versioned_trans.h"
#elif Recorded_Once
#include "versioned_recorded_once.h"
#elif FullyIndirect
#include "versioned_indirect.h"
#else
//#include "versioned_hybrid.h"
#include "versioned_trans.h"
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

#ifdef Transactional
  // trans
  template <typename F>
  auto with_transaction(F f) { //, bool retry=true) {
    in_transaction = true;
    auto r = flck::with_epoch([&] {
      while (true) {
	lock_log_len = 0;
	write_log_len = 0;
	auto x = f();
	if (x.has_value()) {
	  TS t = global_stamp.get_stamp();
	  global_stamp.inc_read_stamp(t);
	  for (int i=0; i < write_log_len; i++)
	    write_log[i]->time_stamp = t;
	}
	for (int i=0; i < lock_log_len; i++)
	  lock_log[i]->unlock();
	//if (!retry || x.has_value()) return x;
	if (x.has_value()) return *x;
      }});
    in_transaction = false;
    return r;
  }
#endif
}


