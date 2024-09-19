#ifndef TLF_STM_H
#define TLF_STM_H

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

// The definition of atomic,
#include "atomic.h"

// The definition of versioned_ptr
#include "versioned_ptr.h"

namespace verlib {
  using tlf_internal::memory_pool;
  using tlf_internal::atomic_region;
  using tlf_internal::atomic_read_only;
  using tlf_internal::versioned;

  // locks are noops
  struct lock {
    template <typename Thunk>
    auto read_lock(Thunk f) { return f();}

    template <typename Thunk>
    auto try_lock(Thunk f) { return f();}
  };

  template <typename T>
  struct versioned_ptr : tlf_internal::lock {
  private:
    tlf_internal::versioned_ptr<T> v;
  public:
    versioned_ptr(T* v) : v(v) {}
    versioned_ptr() : v(nullptr) {}
    T* load() { return read_lock([&] {return v.load();});}
    T* load_weak() { return v.load();}
    void validate() {}
    T* read_snapshot() { return load(); }
    void init(T* x) { v.init(x);}
    void store(T* x) { try_lock([&] {v.store(x); return true;});}
    T* operator=(T* b) {store(b); return b; }
  };

  struct atomic_bool_ : tlf_internal::lock {
  private:
    tlf_internal::atomic_bool v;
  public:
    atomic_bool_(bool x) : v(x) {}
    bool load() { return read_lock([&] {return v.load();});}
    bool load_weak() { return v.load(); }
    void validate() {}
    void store(bool x) { try_lock([&] {v.store(x); return true;});}
    bool operator=(bool b) {store(b); return b; }
  };
  
  struct atomic_bool {
  public:
    atomic_bool(bool x)  {}
    bool load() { return false;}
    void validate() {}
    void store(bool x) { }
    bool operator=(bool b) {store(b); return b; }
  };

}

#endif // TLF_STM_H
