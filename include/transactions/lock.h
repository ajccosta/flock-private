#ifndef TLF_LOCK_H_
#define TLF_LOCK_H_

#include<optional>

namespace verlib {

//thread_local bool in_lock;

struct lock {
private:
  flck::lock lck;
public:
  bool is_locked() { return lck.is_locked();}
  //bool is_self_locked() { return lck.is_self_locked();}
  template <typename Thunk>
  auto read_lock(Thunk f) {
    using RT = decltype(f());
    if (current_transaction != nullptr) {
      bool tmp = in_lock;
      in_lock = true;
      RT result = f();
      in_lock = tmp;
      return result;
    } else return f();
  }

  template <typename Thunk>
  auto try_lock(Thunk f) {
#ifdef Immediate
    if (current_transaction != nullptr) {
      bool taken = false;
      auto r = lck.try_lock_result(f, &taken); // do not release lock
      if (taken) current_transaction->lock_log.add(&lck);
      return r.has_value() && r.value();
    } else return lck.try_lock(f);
#else
    if (current_transaction != nullptr) {
      //if (lck.is_locked() && !lck.is_self_locked()) return false;
      current_transaction->lock_log.add(&lck);
      bool tmp = in_lock;
      in_lock = true;
      bool result = f();
      in_lock = tmp;
      return result;
    } else return lck.try_lock(f);
#endif


  }

  //   template <typename T, typename E>
  //   bool validate(T* v, E expected) {
  //     if (current_transaction != nullptr) {
  // #ifdef Validate
  //       if (try_count < 20) // anywhere from 10-100 seems good
  // 	return v->validate(expected);
  //       else // if too many tries give up on validation and take locks
  // 	return try_lock([=] {return (v->load() == expected);});
  // #else
  //       return try_lock([=] {return (v->load() == expected);});
  // #endif
  //     } else return true;
  //   }

  template <typename Thunk>
  auto try_lock_no_delay(Thunk f) {return try_lock(f);}
};

template <typename F>
bool run_with_locks_rec(int i, transaction_descriptor* descriptor, F f, bool do_help) {
  if (i == descriptor->lock_log.size()) return f();
  if (strict_lock) {
    abort();
    //return descriptor->lock_log[i]->with_lock([=] {
    //return run_with_locks_rec(i+1, descriptor, f, do_help);});
  } else
    return descriptor->lock_log[i]->try_lock([=] {
	return run_with_locks_rec(i+1, descriptor, f, do_help);}
#ifndef NoHelp
      , do_help
#endif
    );
}

template <typename F>
bool run_with_logged_locks(transaction_descriptor* descriptor, F f, bool do_help = true) {
  // sorting actually slows things down slightly (5%) under high contention
  std::sort(descriptor->lock_log.data(),
  	    descriptor->lock_log.data() + descriptor->lock_log.size());
  // if (false)
  //   for (int i=0; i < descriptor->lock_log.size(); i++) {
  //     if (descriptor->lock_log[i]->is_locked() &&
  // 	  !descriptor->lock_log[i]->is_self_locked())
  // 	return false;
  //   }
  return run_with_locks_rec(0, descriptor, f, do_help);
}

}

#endif // TLF_LOCK_H_
