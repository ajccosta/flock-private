#pragma once

#include "flock/flock.h"
#include "timestamps.h"

namespace verlib {

  thread_local long unsigned int* hold_a;
  thread_local int count_a;
  thread_local char* type_a;

  bool valid_pointer(void* ptr) { return ((size_t) ptr) < (1ul << 48);}
  
  // Special timestamps
  const TS tbd = std::numeric_limits<TS>::max()/4;
#ifdef Transactional
  const TS tbs = std::numeric_limits<TS>::max()/4 - 1;
  const TS failed = std::numeric_limits<TS>::max()/4 - 2;
  const TS overwritten = std::numeric_limits<TS>::max()/4 - 3;
#endif  

#ifdef Transactional
  struct trans_descriptor {
    // the thread id the descriptor belongs to
    const int owner; 

    // originally "tbd", then "tbs", then either a real timestamp or "failed"
    std::atomic<TS> time_stamp;  

    void atomic_set_stamp(TS ts) {
      assert(time_stamp.load() != tbd);
      if (time_stamp.load() == tbs) {
	TS old = tbs;
	time_stamp.compare_exchange_strong(old, ts);
      }
    }

    void atomic_set_current_stamp() {
      if (time_stamp.load() == tbs)
	atomic_set_stamp(global_stamp.get_write_stamp());
    }

    trans_descriptor() : time_stamp(tbd), owner(flck::internal::current_id) {
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }
  };

  flck::memory_pool<trans_descriptor> trans_descriptor_pool;
#endif // Transactional

#define init_ptr (((versioned*) nullptr) - 1)

  struct versioned {
  private:
    std::atomic<TS> stamp_or_descriptor;
    std::atomic<versioned*> next_version;

#ifdef Transactional

    // tags second highest bit to mark as descriptor
    constexpr static int bit_num = 62;
    static bool is_tagged(TS sd) {return (sd >> bit_num) & 1;}
    static TS tag_descriptor(trans_descriptor* ptr) {
      return (TS) ptr | (1ul << bit_num);}
    static trans_descriptor* untag_descriptor(TS sd) {
      auto ptr = (trans_descriptor*) (sd & ~(1ul << bit_num));
      assert(valid_pointer((void*) ptr));
      return ptr;
    }

  public:

    std::pair<trans_descriptor*, TS> get_descriptor_and_stamp() {
      TS td = stamp_or_descriptor.load();
      if (is_tagged(td)) return std::pair(untag_descriptor(td), tbd);
      else return std::pair(nullptr, td);
    }

    bool has_self_descriptor() {
      TS td = stamp_or_descriptor.load();
      if (is_tagged(td)) {
	assert(untag_descriptor(td)->owner == flck::internal::current_id);
	return true;
      } else return false;
    }

    void set_descriptor() {
      assert(stamp_or_descriptor.load() == tbd);
      stamp_or_descriptor = tag_descriptor(current_transaction);
    }

    void remove_descriptor(TS ts, bool check_stamp = true) {
      TS td = stamp_or_descriptor.load();
      assert(is_tagged(td));
      assert(untag_descriptor(td)->owner == flck::internal::current_id);
      assert(!check_stamp || untag_descriptor(td)->time_stamp == ts);
      stamp_or_descriptor = ts;
    }

    // once a next version is installed, it becomes recorded
    bool is_recorded() {
      return next_version != init_ptr;}
  
    void set_stamp(TS t) {
      assert(stamp_or_descriptor.load() == tbd);
      next_version = nullptr;
      stamp_or_descriptor = t;
    }
  
    TS get_stamp() {
      TS td = stamp_or_descriptor.load();
      assert(!is_tagged(td));
      return td;
    }
#else
  public:
    bool is_recorded() {return stamp_or_descriptor.load() != tbd;}
    void set_stamp(TS t) { stamp_or_descriptor = t;}
    TS get_stamp() { return stamp_or_descriptor;}
#endif
    versioned() : stamp_or_descriptor(tbd), next_version(init_ptr) {} 
    versioned(versioned* next) : stamp_or_descriptor(tbd), next_version(next) {}
    versioned* get_next_version() {return next_version;}
    void set_next_version(versioned* nv) {
      assert(next_version == init_ptr);  
      next_version = nv;}
    void set_with_current_stamp() {
      TS old_t = tbd;
      TS new_t = global_stamp.get_write_stamp();
      TS td = stamp_or_descriptor.load();
#if Transactional
      assert(!is_tagged(td));    
#endif
      if (td == old_t) 
	stamp_or_descriptor.compare_exchange_strong(old_t, new_t);
    }
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

  void finalize_versioned_store(flck::atomic<versioned*>* loc, TS ts) {
    versioned* ptr = strip_indirect(loc->load());
    assert(ptr != nullptr);
    // splice this latest version out if transaction aborted
    if (ts == failed) *loc = ptr->get_next_version();
    // replace descriptor with timestamp (possibly failed stamp)
    ptr->remove_descriptor(ts); 
  }

  versioned* set_stamp(versioned* tagged_ptr, bool validating = false) {
    if (tagged_ptr == nullptr) return nullptr;
    versioned* ptr = strip_indirect(tagged_ptr);
    auto [descriptor, stamp] = ptr->get_descriptor_and_stamp();
    if (stamp == failed || stamp == overwritten) return ptr->get_next_version();
    if (stamp != tbd) return tagged_ptr;
    if (stamp != tbd) return (stamp == failed) ? ptr->get_next_version() : tagged_ptr;
    if (descriptor == nullptr) {ptr->set_with_current_stamp(); return tagged_ptr; }
    if (descriptor->owner == flck::internal::current_id) return tagged_ptr;
    // at this point there is a descriptor and it is owned by someone else
    TS dts = descriptor->time_stamp.load();
    if (dts == tbd || dts == failed) return ptr->get_next_version();
    auto [ignore, new_stamp] = ptr->get_descriptor_and_stamp();
    if (new_stamp == overwritten) return ptr->get_next_version();
    descriptor->atomic_set_current_stamp();
    return tagged_ptr;
  }

#include "transactions.h"

#else // Not transactional
  versioned* set_stamp(versioned* tagged_ptr) {
    versioned* ptr = strip_indirect(tagged_ptr);
    if (ptr != nullptr && ptr->get_stamp() == tbd) 
      ptr->set_with_current_stamp();
    return tagged_ptr;
  }
#endif

#ifdef Transactional
  using atomic_val = size_t;

  struct hold {
    atomic_val old_val;
    atomic_val new_val;
    trans_descriptor* descriptor;
    constexpr static int bit_num = 63;
    static bool is_held(atomic_val x) {return (x >> bit_num) & 1;}
    static atomic_val tag_hold(hold* ptr) {
      return (TS) ptr | (1ul << bit_num);}
    static hold* untag_hold(atomic_val x) {
      return (hold*) (x & ~(1ul << bit_num)); }
    hold(atomic_val o, atomic_val n, trans_descriptor* d)
      : old_val(o), new_val(n), descriptor(d) {}
  };

  static flck::memory_pool<hold> hold_pool;

  template <typename V> 
  struct atomic {
    std::atomic<atomic_val> v;
    V load() {
      atomic_val x = v.load();
      if (!hold::is_held(x)) return (V) x;
      hold* ptr = hold::untag_hold(x);
      trans_descriptor* td = ptr->descriptor;
      if (td->time_stamp == tbd) return (V) ptr->old_val;
      td->atomic_set_current_stamp();
      return (V) ptr->new_val;
    }
    void store(V x) {
      if (current_transaction == nullptr) v = x;
      else {
	atomic_val y = v.load();
	if (hold::is_held(y)) {
	  hold* ptr = hold::untag_hold(x);
	  trans_descriptor* td = ptr->descriptor;
	  assert(td->owner == flck::internal::current_id);
	  ptr->new_val = (atomic_val) x;
	} else {
	  v = hold::tag_hold(hold_pool.new_obj(y, (atomic_val) x, current_transaction));
	}
      }
    }

    V operator=(V b) {store(b); return b; }
  };

  void finalize_atomic_store(atomic_val* loc) {
    assert(hold::is_held(*loc));
    auto hold_ptr = hold::untag_hold(*loc);
    assert(hold_ptr->descriptor->owner == flck::internal::current_id);
    assert(hold_ptr->descriptor->time_stamp != tbd);
    assert(hold_ptr->descriptor->time_stamp != tbs);
    bool passed = hold_ptr->descriptor->time_stamp != failed;
    *loc = passed ? hold_ptr->new_val : hold_ptr->old_val;
    hold_pool.retire(hold_ptr);
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
      // versioned* x = link_pool.new_obj(nullptr, (versioned*) ptr); //fix
      // x->set_stamp(zero_stamp);
      // return add_indirect(x);
      if (ptr != nullptr && !ptr->is_recorded()) 
	ptr->set_stamp(zero_stamp);
      return ptr;
    }

    bool cas_from_cam(versioned* old_v, versioned* new_v) {
#ifdef NoHelp
      return v.cas(old_v, new_v);
#else
      v.cam(old_v, new_v);
      return (v.load() == new_v ||
	      strip_indirect(new_v)->time_stamp.load() != tbd); 
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
      versioned* ptr = set_stamp(v.read());
      versioned* ptr_ = strip_indirect(ptr);

      // chase down version chain
      while (ptr != nullptr && ptr_->get_stamp() > ls) {
	ptr = ptr_->get_next_version();
	ptr_ = strip_indirect(ptr);
      }
#ifdef LazyStamp
      if (ptr != nullptr && ptr_->get_stamp() == ls
	  && speculative)
	aborted = true;
#endif
      if (is_indirect(ptr)) {
	return (V*) ((ver_link*) ptr_)->value;
      } else return (V*) ptr;
    }

    V* load() {  // can be used anywhere
      assert(this != nullptr);
      if (local_stamp != -1) return read_snapshot();
      auto x = v.load();
      return get_ptr_shortcut(set_stamp(x));
    }
  
#ifdef SlowStore
    void store(V* newv) {
      V* oldv = load();
      cas(oldv, newv);
    }
#else
    void store(V* ptr) {  // Fix, get rid of extra argument
      versioned* old_v = v.load();
      versioned* next = old_v;
#ifdef Transactional
      if (current_transaction != nullptr) {
	if (old_v != nullptr) {
	  auto [td, ts] = strip_indirect(old_v)->get_descriptor_and_stamp();
	  if (td != nullptr && (td->owner != flck::internal::current_id || write_log.cnt == 0)) {
	    TS xx = td->time_stamp;
	    std::cout << "ouch1: " << xx << ", " << td->owner << ", " << flck::internal::current_id << ", " << std::endl;
	    auto [td2, ts2] = strip_indirect(old_v)->get_descriptor_and_stamp();
	    std::cout << "ouch2: " << td2 << ", " << (ts2 == failed) << ", " << (td->time_stamp == failed) << std::endl;
	  }
	}
	if (old_v != nullptr && strip_indirect(old_v)->has_self_descriptor()) { 
	  next = strip_indirect(old_v)->get_next_version();
	  strip_indirect(old_v)->remove_descriptor(overwritten, false);
	} else { 
	  //assert(write_log.cnt == 0 || write_log.vals[write_log.cnt-1] != &v); // fix
	  write_log.add(&v);
	}
      }
#endif
      versioned* new_v = ptr;
      assert(!is_indirect(ptr));
      bool use_indirect = true; //(ptr == nullptr || ptr->is_recorded()); 
      if (use_indirect) 
	new_v = add_indirect(link_pool.new_obj(next, new_v)); 
      else ptr->set_next_version(next);
#ifdef Transactional
      if (current_transaction != nullptr) {
	strip_indirect(new_v)->set_descriptor();
	auto [td, ts] = strip_indirect(new_v)->get_descriptor_and_stamp();  // fix
	assert(td != nullptr && td->owner == flck::internal::current_id);
      } 
#endif
      
#ifdef NoShortcut
      v = new_v;
// #ifdef Transactional
//       // need to comment this out fix
//       if (current_transaction != nullptr) {
// 	for (int i=0; i < write_log.cnt; i++) {
// 	  auto [ds, ts] = strip_indirect(write_log.vals[i]->load())->get_descriptor_and_stamp();
// 	  if (ds == nullptr) std::cout << "yikes: null: " << i << ", " << write_log.cnt << ", " << ts << ", "
// 				       << ptr->key << "^" << strip_indirect(write_log.vals[i]->load())->setter << ", " << write_log.vals[i] << ", " << ptr <<std::endl;
// 	  else if (ds->owner != flck::internal::current_id)
// 	    std::cout << "yikes: bad owner: " << ds->owner << ", " << flck::internal::current_id << std::endl;
// 	  assert(strip_indirect(write_log.vals[i]->load())->has_self_descriptor());
// 	}
//       }
// #endif
      if (is_indirect(old_v))
	link_pool.retire((ver_link*) strip_indirect(old_v));
#else
      assert(false); // fix
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
      if (current_transaction == nullptr)
	strip_indirect(new_v)->set_with_current_stamp();
#else
      set_stamp(new_v);
#endif
      if (use_indirect) shortcut(new_v); 
    }
#endif
  
    //   bool cas(V* exp, V* ptr) {
    // #ifndef NoShortcut
    //     for(int ii = 0; ii < 2; ii++) {
    // #endif
    //       versioned* old_v = v.load();
    //       versioned* new_v = ptr;
    //       V* old = get_ptr(old_v);
    //       set_stamp(old_v);
    //       if(old != exp) return false;
    //       if (exp == ptr) return true;
    //       bool use_indirect = (ptr == nullptr || !ptr->is_unset());

    //       if(use_indirect)
    // 	new_v = add_indirect(link_pool.new_obj(old_v, new_v));
    //       else ptr->next_version = old_v;

    //       if (cas_from_cam(old_v, new_v)) {
    //         set_stamp(new_v);
    //         if (is_indirect(old_v))
    // 	  link_pool.retire((ver_link*) strip_indirect(old_v));
    // #ifndef NoShortcut
    // 	if (use_indirect) shortcut(new_v);
    // #endif
    //         return true;
    //       }
    //       if (use_indirect)
    // 	link_pool.destruct((ver_link*) strip_indirect(new_v));
    // #ifndef NoShortcut
    //     }
    // #endif
    //     set_stamp(v.load());
    //     return false;
    //   }

    V* operator=(V* b) {store(b); return b; }

  };

  template <typename T, typename E>
  bool validate(flck::lock& lck, T* v, E expected) {
#ifdef Transactional  // Fixed!! from TransUndo
    if (current_transaction != nullptr) {
      return lck.try_lock([=] {return (v->load() == expected);});
    } else return true;
#else
    return true;
#endif
  }

} // namespace verlib
