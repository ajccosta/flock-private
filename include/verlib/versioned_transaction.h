#pragma once

#include "flock/flock.h"
//#include "flock/acquired_pool.h"
#include "timestamps.h"

thread_local size_t check_array[1000];

namespace verlib {

  thread_local long unsigned int* hold_a;
  thread_local int count_a;
  thread_local char* type_a;

  bool valid_pointer(void* ptr) { return ((size_t) ptr) < (1ul << 48);}
  
  // Special timestamps
  const TS tbd = std::numeric_limits<TS>::max()/4;
  const TS tbs = std::numeric_limits<TS>::max()/4 - 1;
  const TS failed = std::numeric_limits<TS>::max()/4 - 2;

  struct trans_descriptor {
    // the thread id the descriptor belongs to
    const int owner; 

    // originally "tbd", then "tbs", then either a real timestamp or "failed"
    std::atomic<TS> time_stamp;  

    //std::atomic<bool> acquired;   

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
  //  flck::internal::acquired_pool<trans_descriptor> trans_descriptor_pool;

#define init_ptr (((versioned*) nullptr) - 1)

  struct versioned {
  private:
    std::atomic<TS> stamp_or_descriptor;
    std::atomic<versioned*> next_version;

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
      if (next_version.load() == init_ptr) {
	//assert(stamp_or_descriptor.load() == tbd);
	return false;
      } else return true;
    }

    void set_stamp(TS t) {
      assert(stamp_or_descriptor.load() == tbd);
      next_version = nullptr;
      stamp_or_descriptor = t;
    }

    void set_zero() {
      assert(next_version == init_ptr);
      //assert(stamp_or_descriptor.load() == tbd);
      stamp_or_descriptor = zero_stamp;
    }

    void set_stamp_and_version(TS t, versioned* ver) {
      assert(stamp_or_descriptor.load() == zero_stamp);
      next_version = ver;
      stamp_or_descriptor = t;
    }

    TS get_stamp() {
      TS td = stamp_or_descriptor.load();
      assert(!is_tagged(td));
      return td;
    }
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
      assert(!is_tagged(td));    
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

  void finalize_versioned_store(std::tuple<flck::atomic<versioned*>*,bool> loc_dir, TS ts) {
    auto [loc, direct] = loc_dir;
    versioned* ptr = strip_indirect(loc->load());
    assert(ptr != nullptr);
    // splice this latest version out if transaction aborted
    if (ts == failed) *loc = ptr->get_next_version();
    else {
      versioned* val_ptr = ((ver_link*) ptr)->value;
      // try to shortcut indirect
      if (val_ptr != nullptr && !val_ptr->is_recorded()) {
	val_ptr->set_stamp_and_version(ts, ptr->get_next_version());
	*loc = val_ptr;
	link_pool.retire((ver_link*) ptr);
	return;
      }
    }
    // replace descriptor with timestamp (possibly failed stamp)
    ptr->remove_descriptor(ts); 
  }

  versioned* set_stamp(versioned* tagged_ptr, bool validating = false) {
    if (tagged_ptr == nullptr) return nullptr;
    versioned* ptr = strip_indirect(tagged_ptr);
    auto [descriptor, stamp] = ptr->get_descriptor_and_stamp();
    if (stamp == failed) return ptr->get_next_version();
    if (stamp != tbd) return tagged_ptr;
    if (stamp != tbd) return (stamp == failed) ? ptr->get_next_version() : tagged_ptr;
    if (descriptor == nullptr) {ptr->set_with_current_stamp(); return tagged_ptr; }
    if (descriptor->owner == flck::internal::current_id) return tagged_ptr;
    // at this point there is a descriptor and it is owned by someone else
    TS dts = descriptor->time_stamp.load();
    if (dts == tbd || dts == failed) return ptr->get_next_version();
#ifdef Validate
    if (validating && dts == tbs &&
	descriptor->owner < flck::internal::current_id) { 
      // kill other if they have lower id. prevents validate cycle
      TS old = tbs;
      descriptor->time_stamp.compare_exchange_strong(old,failed);
    } else //otherwise wait until other is done (could also help)
      while (descriptor->time_stamp.load() == tbs);
    if (descriptor->time_stamp.load() == failed) return ptr->get_next_version();
#else
    descriptor->atomic_set_current_stamp();
#endif
    return tagged_ptr;
  }

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

  void finalize_atomic_store(std::atomic<atomic_val>* loc) {
    assert(hold::is_held(*loc));
    auto hold_ptr = hold::untag_hold(*loc);
    assert(hold_ptr->descriptor->owner == flck::internal::current_id);
    assert(hold_ptr->descriptor->time_stamp != tbd);
    assert(hold_ptr->descriptor->time_stamp != tbs);
    bool passed = hold_ptr->descriptor->time_stamp != failed;
    *loc = passed ? hold_ptr->new_val : hold_ptr->old_val;
    hold_pool.retire(hold_ptr);
  }

#include "transactions.h"

  template <typename V> 
  struct atomic {
    std::atomic<atomic_val> v;
    atomic(V v) : v(v) {}
    V load() {
      atomic_val x = v.load();
      if (!hold::is_held(x)) return (V) x;
      hold* ptr = hold::untag_hold(x);
      trans_descriptor* td = ptr->descriptor;
      if (td->time_stamp == tbd) return (V) ptr->old_val;
      td->atomic_set_current_stamp();
      return (V) ptr->new_val;
    }
    void store(V new_v) {
      if (current_transaction == nullptr) v = new_v;
      else {
	atomic_val old_v = v.load();
	if (hold::is_held(old_v)) {
	  hold* ptr = hold::untag_hold(old_v);
	  trans_descriptor* td = ptr->descriptor;
	  assert(td->owner == flck::internal::current_id);
	  ptr->new_val = (atomic_val) new_v;
	} else {
	  write_atomic_log.add(&v);
	  hold* ptr = hold_pool.new_obj(old_v, (atomic_val) new_v, current_transaction);
	  v = hold::tag_hold(ptr);
	}
      }
    }

    V operator=(V b) {store(b); return b; }
  };
  
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
       // if (current_transaction != nullptr)
       // 	for (int i=0; i < write_log.cnt; i++)
       // 	  if (write_log.vals[i].first == &v)
       // 	    return (V*) write_log.vals[i].second;
      auto x = v.load();
      return get_ptr_shortcut(set_stamp(x));
    }
  
    void store(V* ptr) {  // Fix, get rid of extra argument
      versioned* old_v = v.load();
      bool use_indirect = (ptr == nullptr || ptr->is_recorded());
      if (current_transaction != nullptr) {
	if (!use_indirect) { ptr->set_zero(); }
	// if current transaction already wrote to this location.
	if (old_v != nullptr && strip_indirect(old_v)->has_self_descriptor()) {
	  for (int i=0; i < write_log.cnt; i++)
	    if (write_log.vals[i].first == &v) 
	      write_log.vals[i].second == ptr;
	  ((ver_link*) strip_indirect(old_v))->value = ptr;
	  return;}
	write_log.add(std::pair(&v,ptr));
	versioned* new_v = link_pool.new_obj(old_v, ptr);
	new_v->set_descriptor();
	v = add_indirect(new_v);
      } else { // not in a transaction
	versioned* new_v = ptr;
	if (use_indirect) new_v = add_indirect(link_pool.new_obj(old_v, new_v));
	else ptr->set_next_version(old_v);
	v = new_v;
	strip_indirect(new_v)->set_with_current_stamp();
      }
      if (is_indirect(old_v))
	link_pool.retire((ver_link*) strip_indirect(old_v));
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
  bool validate(flck::lock& lck, T* v, E expected) {
    if (current_transaction != nullptr) {
#ifdef Validate
    if (try_count < 20) // anywhere from 10-100 seems good
      return validate_(v, expected);
    else // if too many tries give up on validation and take locks
      return lck.try_lock([=] {return (v->load() == expected);});
#else
      return lck.try_lock([=] {return (v->load() == expected);});
#endif
    } else return true;
  }

} // namespace verlib
