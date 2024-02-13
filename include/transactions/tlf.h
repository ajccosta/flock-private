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

namespace tlf {

  // template<class T, class Enable = void>
  // using atomic = void;
  
  // template<class T, typename std::enable_if<std::is_same<T,bool>::value>::type>  
  // using atomic = typename verlib::atomic_bool;

  // template<class T, typename = typename std::enable_if<std::is_pointer<T>::value>::type>
  // using atomic = typename verlib::versioned_ptr<std::remove_pointer<T>>;

  template<typename T>
  using atomic = std::conditional_t<std::is_same_v<T, bool>,
				    verlib::atomic_bool,
				    verlib::versioned_ptr<std::remove_pointer_t<T>>>;

  // template<class T, class Enable = void>
  // struct atomic {};

  // template<class T>
  // struct atomic<T, typename std::enable_if<std::is_same<T,bool>::value>::type> : public verlib::atomic_bool {
  //   using verlib::atomic_bool::atomic_bool;
  //   T operator=(T b) {this->store(b); return b; }
  // };

  // template<class T>
  // struct atomic<T, typename std::enable_if<std::is_pointer<T>::value>::type>
  //   : public verlib::versioned_ptr<typename std::remove_pointer<T>::type> {
  //   using verlib::versioned_ptr<typename std::remove_pointer<T>::type>::versioned_ptr;
  //   T operator=(T b) {this->store(b); return b; }
  // };

} // tlf


#endif // TLF_DEFS_H_
