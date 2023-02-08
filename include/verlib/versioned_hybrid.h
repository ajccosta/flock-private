#pragma once

#include "flock/flock.h"
#include "timestamps.h"

namespace verlib {

  thread_local long unsigned int* hold_a;
  thread_local int count_a;
  thread_local int type_a;
  
void check_pointer(void* ptr, std::string str) {
  if (((size_t) ptr) >> 48 != 0) {
    std::cout << "bad pointer in " << str << ": " << ptr << std::endl;
    abort();
  }
}

  #ifdef Transactional
struct trans_descriptor {
  int owner;
  std::atomic<TS> time_stamp;
  trans_descriptor() : time_stamp(tbd), owner(flck::internal::current_id) {}
};

flck::memory_pool<trans_descriptor> trans_descriptor_pool;

  // for storing transaction descriptor on a time stamp
  // constexpr unsigned long descriptor_mask = (1ul << 62);
  // TS set_descriptor(trans_descriptor* td) {
  //   return ((TS) td) | descriptor_mask;}
  // bool is_descriptor(TS ts) {
  //   return (ts & descriptor_mask) != 0;}
  // trans_descriptor* get_descriptor(TS ts) {
  //   return (trans_descriptor*) (~descriptor_mask & ts);}
  #endif

struct versioned {
  flck::atomic_write_once<TS> time_stamp;
  versioned* next_version;
#ifdef Transactional
  trans_descriptor* trans;
  int op;
  bool not_written() {
    return time_stamp.load() == tbd || trans != nullptr;}
  trans_descriptor* get_descriptor() {return trans;}
  TS get_stamp() {return time_stamp.load();}
  void set_current_trans() {
     trans = current_transaction;
  }
  void clear_current_trans(TS ts) {
    time_stamp = ts;
    trans = nullptr;
  }
  versioned() : op(-1), time_stamp(tbd), trans(nullptr) {} 
  versioned(versioned* next) : op(-1), time_stamp(tbd), trans(nullptr), next_version(next) {}
#else
  bool not_written() {return time_stamp.load() == tbd;}
  versioned() : time_stamp(tbd) {}
  versioned(versioned* next) : time_stamp(tbd), next_version(next) {}
#endif
};

  // uses lowest bit of pointer to indicate whether indirect (1) or not (0)
  versioned* add_indirect(versioned* ptr) {
    return (versioned*) (1ul | (size_t) ptr);};
  versioned* strip_indirect(versioned* ptr) {
    return (versioned*) ((size_t) ptr & ~1ul);}
  bool is_indirect(versioned* ptr) {
    return (size_t) ptr & 1;}

struct ver_link : versioned {
  versioned* value;
  ver_link(versioned* next, versioned* value) : versioned{next}, value(value) {}  
};

flck::memory_pool<ver_link> link_pool;

  #ifdef Transactional
  void undo_versioned_write(flck::atomic<versioned*>* loc, bool flag=false) {
    versioned* ptr = loc->load();
    versioned* ptr_ = strip_indirect(ptr);
    trans_descriptor* td = ptr_->get_descriptor();
    if (td == nullptr || td->owner != flck::internal::current_id) {
      std::cout << "bad undo:" << std::endl;
      std::cout << "owner = " << td->owner << ", proc_id = " << flck::internal::current_id << ", in store = " << flag << ", time_stamp = " << td->time_stamp << std::endl;
      abort();
    }
    *loc = ptr_->next_version;
    //if (is_indirect(ptr)) link_pool.retire((ver_link*) ptr_);
  }

  #include "transactions.h"
#endif



template <typename V>
struct versioned_ptr {
private:
  flck::atomic<versioned*> v;

  static void shortcut(versioned* ptr) {
#ifndef NoShortcut
    ver_link* ptr_ = (ver_link*) strip_indirect(ptr);
    if (ptr_->time_stamp.load_ni() <= done_stamp) {
#ifdef NoHelp
      if (v.cas(ptr, ptr_->value))
	link_pool.retire(ptr_);
#else
      if (v.cas_ni(ptr, ptr_->value))
	link_pool.retire_ni(ptr_);
#endif
    }
#endif
  }

  static V* get_ptr(versioned* ptr) {
    versioned* ptr_ = strip_indirect(ptr);
    if (!is_indirect(ptr)) return (V*) ptr_;
    return (V*) ((ver_link*) ptr_)->value;
  }

  static V* get_ptr_shortcut(versioned* ptr) {
    versioned* ptr_ = strip_indirect(ptr);
    if (!is_indirect(ptr)) return (V*) ptr_;
    shortcut(ptr);
    return (V*) ((ver_link*) ptr_)->value;
  }

#ifdef Transactional
  static versioned* set_stamp(versioned* ptr) {
    versioned* ptr_ = strip_indirect(ptr);
    if (ptr_ != nullptr) {
      TS t = ptr_->get_stamp();
      trans_descriptor* td = ptr_->get_descriptor();
      if (t == tbd && td == nullptr) {
	TS new_t = global_stamp.get_write_stamp();
	if (ptr_->time_stamp.load() == tbd)
	  ptr_->time_stamp.cas_ni(tbd, new_t);
      } else if (td != nullptr) {
	if (td->owner != flck::internal::current_id) {
	  if (td->time_stamp.load() == tbd)
	    return ptr_->next_version;
	  else {
#ifdef Validate
	    while (td->time_stamp.load() == tbs);
	    if (td->time_stamp.load() == failed)
	      return ptr_->next_version;
#else
	    if (td->time_stamp.load() == tbs)
	      set_transaction_stamp(td);
#endif
	  }
	}
      }
    }
    return ptr;
  }
#else
  static versioned* set_stamp(versioned* ptr) {
    versioned* ptr_ = strip_indirect(ptr);
    if (ptr != nullptr && ptr_->time_stamp.load_ni() == tbd) {
      TS t = global_stamp.get_write_stamp();
      if(ptr_->time_stamp.load_ni() == tbd)
        ptr_->time_stamp.cas_ni(tbd, t);
    }
    return ptr;
  }
#endif
  
  static versioned* set_zero_stamp(V* ptr) {
    if (ptr != nullptr) {
#ifdef TransUndo
      trans_descriptor* td = ptr->get_descriptor();
      if (td != nullptr) {
	if (td->owner != flck::internal::current_id) {
	  std::cout << "set_zero error: " << td->owner << ", " << flck::internal::current_id << std::endl;
	  abort();
	}
      }
#endif
      if (ptr->time_stamp.load() == tbd) 
	ptr->time_stamp = zero_stamp;
    }
    return ptr;
  }

  bool cas_from_cam(versioned* old_v, versioned* new_v) {
#ifdef NoHelp
    return v.cas(old_v, new_v);
#else
    v.cam(old_v, new_v);
    return (v.load() == new_v ||
	    strip_indirect(new_v)->time_stamp.load() != tbd); // FIX
#endif
  }

public:
  
  versioned_ptr(): v(nullptr) {}
  versioned_ptr(V* ptr) : v(set_zero_stamp(ptr)) {}

  ~versioned_ptr() {
    versioned* ptr = v.load();
    if (is_indirect(ptr))
      link_pool.destruct((ver_link*) strip_indirect(ptr));
  }

  void init(V* ptr) {v = set_zero_stamp(ptr);}

#ifdef Validate
  bool validate(V* expected, flck::atomic_write_once<bool>* deleted) {
    versioned* ptr = v.load();
    versioned* ptr_ = strip_indirect(ptr);
    V* cur = (V*) (is_indirect(ptr) ? ((ver_link*) ptr_)->value : ptr);
    if (cur != expected) return false;
    validate_log.add(std::tuple(&v, ptr, &(deleted->v)));
    return true;
  }
#else

#endif
  
  V* read_snapshot() {
    TS ls = local_stamp;
    versioned* head = set_stamp(v.read());
    versioned* head_unmarked = strip_indirect(head);

    // chase down version chain
    while (head != nullptr && head_unmarked->time_stamp.load() > ls) {
      head = head_unmarked->next_version;
      head_unmarked = strip_indirect(head);
    }
#ifdef LazyStamp
    if (head != nullptr && head_unmarked->time_stamp.load() == ls
	&& speculative)
      aborted = true;
#endif
    if (is_indirect(head)) {
      return (V*) ((ver_link*) head_unmarked)->value;
    } else return (V*) head;
  }

  V* load() {  // can be used anywhere
    if (local_stamp != -1) return read_snapshot();
    auto x = v.load();
    //check_pointer(x, "load");
    return get_ptr_shortcut(set_stamp(x));
  }
  
  V* read() {  // only safe on journey
    return get_ptr_shortcut(v.read());
  }

  void validate() {
    set_stamp(v.load());     // ensure time stamp is set
  }


#ifdef SlowStore
  void store(V* newv) {
    V* oldv = load();
    cas(oldv, newv);
  }
#else
  void store(V* ptr, int op=0) {
    versioned* old_v = v.load();
    versioned* next = old_v;
#ifdef Transactional
    if (current_transaction != nullptr) {
      if (old_v != nullptr) {
	versioned* old_v_ = strip_indirect(old_v);
	trans_descriptor* td = old_v_->get_descriptor();
	if (td != nullptr) { // second write by same transaction
	  //std::cout << ".";
	  next = old_v_->next_version;
	  assert(td->owner == flck::internal::current_id);
	  // if (td->owner != flck::internal::current_id) {
	  //   std::cout << "oops: " << td->owner << ", " << flck::internal::current_id << ", " << td->time_stamp << ", " << ptr->key << ", " << get_ptr(old_v)->key <<
	  //     std::endl;
	  //   abort();}
	  //undo_versioned_write(&v, true);
	  //old_v = old_v_->next_version;
	  //old_v = v.load(); // should do the same as commented line above
	} else {
	  write_log.add(&v);
	}
      } 
    }
#endif
    versioned* new_v = ptr;
    bool use_indirect = (ptr == nullptr || !ptr->not_written());
    if (use_indirect) 
      new_v = add_indirect(link_pool.new_obj(next, new_v)); 
    else ptr->next_version = next;
#ifdef Transactional
    strip_indirect(new_v)->set_current_trans();
#endif
      
#ifdef NoShortcut
    v = new_v;
    if (is_indirect(old_v))
      link_pool.retire((ver_link*) strip_indirect(old_v));
#else
    v.cam(old_v, new_v);
    if (is_indirect(old_v)) {
      versioned* val = v.load();
      ver_link* old_l = (ver_link*) strip_indirect(old_v);
      if (val != old_l->value)
	link_pool.retire(old_l);
      else v.cam(val, new_v);
    }
#endif
#ifdef Transactional
    if (current_transaction == nullptr) {
      versioned* ptr_ = strip_indirect(new_v);
      TS t = global_stamp.get_write_stamp();
      ptr_->time_stamp.cas_ni(tbd, t);
    } 
#else
    set_stamp(new_v);
#endif
    if (use_indirect) shortcut(new_v);
  }
#endif
  
  bool cas(V* exp, V* ptr) {
#ifndef NoShortcut
    for(int ii = 0; ii < 2; ii++) {
#endif
      versioned* old_v = v.load();
      versioned* new_v = ptr;
      V* old = get_ptr(old_v);
      set_stamp(old_v);
      if(old != exp) return false;
      if (exp == ptr) return true;
      bool use_indirect = (ptr == nullptr || !ptr->is_unset());

      if(use_indirect)
	new_v = add_indirect(link_pool.new_obj(old_v, new_v));
      else ptr->next_version = old_v;

      if (cas_from_cam(old_v, new_v)) {
        set_stamp(new_v);
        if (is_indirect(old_v))
	  link_pool.retire((ver_link*) strip_indirect(old_v));
#ifndef NoShortcut
	if (use_indirect) shortcut(new_v);
#endif
        return true;
      }
      if (use_indirect)
	link_pool.destruct((ver_link*) strip_indirect(new_v));
#ifndef NoShortcut
    }
#endif
    set_stamp(v.load());
    return false;
  }

  V* operator=(V* b) {store(b); return b; }
};

template <typename T>
bool validate(flck::lock& lck, versioned_ptr<T>* ptr, T* expected,
	      flck::atomic_write_once<bool>* deleted) {
#ifdef TransUndo
#ifdef Validate
  return ptr->validate(expected, deleted);
#else
  return lck.try_lock([=] {
    return (!deleted->load() && ptr->load() == expected);});
#endif
#else
  return true;
#endif
}

} // namespace verlib
