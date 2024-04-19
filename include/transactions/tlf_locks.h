#ifndef TLF_LOCK_I_H_
#define TLF_LOCK_I_H_

#include<optional>

namespace tlf {

struct shared_mutex {
  flck::lock lck;
  bool try_lock() {
    if (verlib::current_transaction != nullptr) {
      verlib::current_transaction->lock_log.add(lck.get_lock());
      verlib::in_lock++;
      return true;
    } else return lck.try_lock_no_unlock();
  }

  void lock() {
    if (verlib::current_transaction != nullptr) {try_lock();}
    else lck.lock_no_unlock();
  }

  void unlock() {
    if (verlib::current_transaction != nullptr) 
      verlib::in_lock--;
    else lck.unlock();
  }

  bool try_lock_shared() {
    if (verlib::current_transaction != nullptr) {
      verlib::in_lock++;
      return true;
    } else return true;
  }

  void lock_shared() { try_lock_shared();}

  void unlock_shared() {
    if (verlib::current_transaction != nullptr) verlib::in_lock--;
  }

};

} // namespace tlf

#endif // TLF_LOCK_I_H_
