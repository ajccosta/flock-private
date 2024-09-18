#ifndef TLF_DEFS_H_
#define TLF_DEFS_H_

#include <shared_mutex>
#include "flock/flock.h"
#include "flock/acquired_pool.h"
#include "../verlib/timestamps.h"

// defines the transaction_descriptor structure
#include "transaction_descriptor.h"

// memory pool to handle aborted allocates and retires
#include "memory_pool.h"

// the main functions 
#include "core_functions.h"

// The lock interface
#include "lock.h"
#include "tlf_locks.h"

// The definition of atomic,
#include "atomic.h"

// The definition of versioned_ptr
#include "versioned_ptr.h"

// this is the full interface
namespace tlf {
  // currently only supports atomics that are pointers or bools
  template<typename T>
  using atomic = std::conditional_t<std::is_same_v<T, bool>,
				    verlib::atomic_bool,
				    verlib::versioned_ptr<std::remove_pointer_t<T>>>;
  using tlf_internal::atomic_region;
  using tlf_internal::atomic_read_only;
  using tlf_internal::shared_mutex;
  using tlf_internal::New;
  using tlf_internal::Retire;

  // some functions for managing the memory pool
  using tlf_internal::pool_clear;
  using tlf_internal::pool_stats;

} // tlf


#endif // TLF_DEFS_H_
