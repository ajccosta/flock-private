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
#include <parlay/sequence.h>

#ifdef Transactional
// some definitions needed by flock locks (blocking or lock free) when
// using transactions, so must be defined before including flock.
#include <array>
namespace flck {
  template <typename T> struct atomic;
  template <typename T> struct atomic_write_once;
  namespace internal {  struct lock; }}

constexpr int log_len = 300;

// template <typename T>
// struct transaction_log {
//   std::array<T,log_len> vals;
//   int cnt;
//   void add(T v) {
//     if (cnt == log_len) {
//       std::cout << "overfull log" << std::endl;
//       abort();}
//     vals[cnt++] = v;
//   }
//   void clear() { cnt = 0;}
//   transaction_log() : cnt(0) {}
// };

const int full_len = 20;

template <typename T>
struct transaction_log {
private:
  std::array<T,full_len> vals;
  parlay::sequence<T> extra_vals;
  int cnt;
public:
  int size() {return cnt;}
  void add(T v) {
    if (cnt < full_len) {vals[cnt++] = v; return;}
    if (cnt == full_len) {
      extra_vals.reserve(2*full_len);
      for (int i=0; i < full_len; i++)
	extra_vals.push_back(vals[i]);
    }
    extra_vals.push_back(v);
    cnt++;
  }
  T* data() {
    return (cnt > full_len) ? extra_vals.data() : &vals[0];}
  void clear() { cnt = 0; extra_vals.clear();}
  T& operator [](int i) {
    return (cnt > full_len) ? extra_vals[i] : vals[i];}
  transaction_log() : cnt(0) {}
};

namespace verlib { // trans
  struct trans_descriptor;
  struct versioned;
  thread_local trans_descriptor* current_transaction = nullptr;
  using atomic_val = size_t;

  // All the logs
  thread_local transaction_log<flck::internal::lock*> lock_log;
  //thread_local transaction_log<std::pair<std::atomic<atomic_val>*,atomic_val>> write_atomic_log;
  //thread_local transaction_log<std::tuple<flck::atomic<versioned*>*,versioned*>> validate_ptr_log;
  //thread_local transaction_log<std::tuple<std::atomic<atomic_val>*,atomic_val>> validate_atomic_log;
  thread_local transaction_log<bool*> retired_log;
  thread_local transaction_log<bool*> allocated_log;
}
#endif

#include "flock/flock.h"

#ifdef Versioned

// versioned objects, ptr_type includes version chains
#ifdef Recorded_Once
#include "versioned_recorded_once.h"
#elif FullyIndirect
#include "versioned_indirect.h"
#elif Transactional
#include "versioned_transaction.h"
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

template <typename xT>
struct memory_pool {
  using T = xT;
  flck::memory_pool<T> pool;
  void reserve(size_t n) { pool.reserve(n);}
  void shuffle(size_t n) { pool.shuffle(n);}
  void stats() { pool.stats();}
  void clear() { pool.clear();}
  void destruct(T* ptr) { pool.destruct(ptr); }
  template <typename ... Args>
  T* new_obj(Args... args) { return pool.new_obj(args...);}
  template <typename F, typename ... Args>
  T* new_init(F f, Args... args) { return pool.new_init(f, args...);}
  void retire(T* ptr) {pool.retire(ptr);}
#ifdef TransUndo
  void retire_undo(T* ptr, trans_descriptor* d) {pool.retire_undo(ptr, d);}
#else
  void retire_undo(T* ptr, void* d) {pool.retire_undo(ptr, d);}
#endif
};
}
