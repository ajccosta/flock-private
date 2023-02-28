#pragma once

#include <parlay/parallel.h> // needed for worker_id

#include <atomic>
#include<thread>
#include<optional>

thread_local bool in_lock;
struct lock {
private:
  flck::lock lck;
public:
  bool is_locked() { return lck.is_locked();}
  bool is_self_locked() { return lck.is_self_locked();}
  
  template <typename Thunk>
  auto try_lock(Thunk f) {
#ifdef Immediate
    return lck.try_lock(f);
#else
    if (current_transaction != nullptr) { 
      verlib::lock_log.add(&lck);
      bool tmp = in_lock;
      in_lock = true;
      bool result = f();
      in_lock = tmp;
      return result;
    } else return lck.try_lock(f);
#endif
  }

  template <typename Thunk>
  auto try_lock_no_delay(Thunk f) {return try_lock(f);}
};

  template <typename F>
  bool run_with_locks_rec(int i, F& f) {
    if (i == lock_log.cnt) return f();
    return lock_log.vals[i]->try_lock([&] {
	return run_with_locks_rec(i+1, f);});
  }

  template <typename F>
  bool run_with_logged_locks(F f, bool validate=false) {
    std::sort(lock_log.vals.begin(), lock_log.vals.begin()+lock_log.cnt);
    if (validate)
      for (int i=0; i < validate_ptr_log.cnt; i++) {
	auto [location, expected] = validate_ptr_log.vals[i];
	if (set_stamp(location->load()) != expected) return false;
      }
    // for (int i=0; i < lock_log.cnt; i++) {
    //   if (lock_log.vals[i]->is_locked() && !lock_log.vals[i]->is_self_locked())
    // 	return false;
    // }
    return run_with_locks_rec(0, f);
  }
