#pragma once

#include "flock/flock.h"
#include "timestamps.h"

namespace verlib {

  thread_local long unsigned int* hold_a;
  thread_local int count_a;
  thread_local int type_a;

#ifdef Transactional
struct trans_descriptor {
  int owner;
  std::atomic<TS> time_stamp;
  trans_descriptor() : time_stamp(tbd), owner(flck::internal::current_id) {}
};

flck::memory_pool<trans_descriptor> trans_descriptor_pool;
#endif // Transactional

struct versioned {
  std::atomic<TS> time_stamp;
  versioned* next_version;
#ifdef Transactional
  trans_descriptor* trans;
  int op;
  bool written() {
    return time_stamp.load() != tbd || trans != nullptr;}
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
  bool written() {return time_stamp.load() != tbd;}
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
    assert(ptr_ != nullptr);
    trans_descriptor* td = ptr_->get_descriptor();
    assert(td != nullptr && td->owner == flck::internal::current_id);
    *loc = ptr_->next_version;
  }

// set the stamp on the transaction descriptor
TS set_transaction_stamp(trans_descriptor* desc) {
  TS t = global_stamp.get_write_stamp();
  TS old = tbs;
  if (desc->time_stamp.load() == tbs)
    desc->time_stamp.compare_exchange_strong(old, t);
  return desc->time_stamp.load();
}

  versioned* set_stamp(versioned* ptr, bool validating=false) {
    versioned* ptr_ = strip_indirect(ptr);
    if (ptr_ != nullptr) {
      TS t = ptr_->get_stamp();
      trans_descriptor* td = ptr_->get_descriptor();
      if (t == tbd && td == nullptr) {
	TS new_t = global_stamp.get_write_stamp();
	if (ptr_->time_stamp.load() == tbd) {
	  TS old_t = tbd;
	  ptr_->time_stamp.compare_exchange_strong(old_t, new_t);
	}
      } else if (td != nullptr) {
	if (td->owner != flck::internal::current_id) {
	  if (td->time_stamp.load() == tbd)
	    return ptr_->next_version;
	  else {
#ifdef Validate
	    if (validating && td->time_stamp.load() == tbs && td->owner < flck::internal::current_id) { 
	      // kill other if they have lower id. prevents wait cycle
	      TS old = tbs;
	      td->time_stamp.compare_exchange_strong(old,failed);
	    } else //otherwise wait until other is done (could also help)
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

  #include "transactions.h"

#else // Not transactional
  versioned* set_stamp(versioned* ptr) {
    versioned* ptr_ = strip_indirect(ptr);
    if (ptr != nullptr && ptr_->time_stamp.load() == tbd) {
      TS t = global_stamp.get_write_stamp();
      if(ptr_->time_stamp.load() == tbd) {
	TS old_t = tbd;
        ptr_->time_stamp.compare_exchange_strong(old_t, t);
      }
    }
    return ptr;
  }
#endif


template <typename V>
struct versioned_ptr {
  //private:
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

  
  static versioned* set_zero_stamp(V* ptr) {
    if (ptr != nullptr) {
#ifdef TransUndo
      trans_descriptor* td = ptr->get_descriptor();
      if (td != nullptr)
	assert(td->owner == flck::internal::current_id);
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
    assert(this != nullptr);
    if (local_stamp != -1) return read_snapshot();
    auto x = v.load();
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
	  // if (td->owner != flck::internal::current_id || td->pad != 11) {
	  //   std::cout << "oops: " << (void*) td->pad << ", " << td->owner << ", " << td->time_stamp.load() << ", " << td->check_val << "," << flck::internal::current_id << std::endl;
	  //   abort();
	  // }
	  assert(td->owner == flck::internal::current_id);
	} else write_log.add(&v);
      } else write_log.add(&v); 
    }
#endif
    versioned* new_v = ptr;
    bool use_indirect = (ptr == nullptr || ptr->written());
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
      TS old_t = tbd;
      ptr_->time_stamp.compare_exchange_strong(old_t, t);
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
#ifdef Validate
  bool validate(V* expected) {
    assert(this != nullptr);
    versioned* ptr = set_stamp(v.load());
    V* cur = (V*) (is_indirect(ptr) ? ((ver_link*) strip_indirect(ptr))->value : ptr);
    if (cur != expected) return false; 
    validate_ptr_log.add(std::tuple(&v, ptr));
    return true;
  }
#endif

};

#ifdef Validate
  template <typename T>
  bool validate_(versioned_ptr<T>* ptr, T* expected) {return ptr->validate(expected); }

  bool validate_(flck::atomic_write_once<bool>* a, bool expected) {
    if (a->load() != expected) return false;
    validate_val_log.add(std::tuple(a, expected));
    return true;
  }
#endif

template <typename T, typename E>
bool validate(flck::lock& lck, T* ptr, E expected) {
#ifdef TransUndo
  if (current_transaction != nullptr) {
#ifdef Validate
    if (try_count < 20) // anywhere from 10-100 seems good
      return validate_(ptr, expected);
    else // if too many tries give up on validation and take locks
      return lck.try_lock([=] {return (ptr->load() == expected);});
#else
    return lck.try_lock([=] {return (ptr->load() == expected);});
#endif
  } else return true;
#else
  return true;
#endif
}
  

} // namespace verlib
