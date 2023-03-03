#pragma once

#include "flock/flock.h"
#include "flock/acquired_pool.h"
#include "timestamps.h"

namespace verlib {

  thread_local bool in_main_loop = false;
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

    std::atomic<bool> acquired;   

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

  //flck::memory_pool<trans_descriptor> trans_descriptor_pool;
  flck::internal::acquired_pool<trans_descriptor> trans_descriptor_pool;

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
      if (is_tagged(td)) {
      	trans_descriptor* descriptor = untag_descriptor(td);
      	trans_descriptor_pool.acquire(descriptor);
      	td = stamp_or_descriptor.load();
      } 
      if (is_tagged(td)) return std::pair(untag_descriptor(td), tbd);
      else return std::pair(nullptr, td);
    }

    void set_descriptor(trans_descriptor* descriptor) {
      assert(stamp_or_descriptor.load() == tbd);
      stamp_or_descriptor = tag_descriptor(descriptor);
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

    TS get_stamp() {
      TS td = stamp_or_descriptor.load();
      assert(!is_tagged(td));
      return td;
    }

    TS get_stamp_indirect() {
      TS td = stamp_or_descriptor.load();
      if (is_tagged(td)) return tbd;
      else return td;
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

  versioned* install_versioned_store(flck::atomic<versioned*>* loc,
				     versioned* ptr,
				     trans_descriptor* descriptor) {
    versioned* old_v = loc->load();
    bool use_indirect = (ptr == nullptr || ptr->is_recorded());
    versioned* new_v = ptr;
    if (use_indirect) new_v = add_indirect(link_pool.new_obj(old_v, new_v));
    else ptr->set_next_version(old_v);
    if (descriptor != nullptr)
      strip_indirect(new_v)->set_descriptor(descriptor);
#ifdef NoShortcut
    *loc = new_v;
    if (is_indirect(old_v))
      link_pool.retire((ver_link*) strip_indirect(old_v));
#else
    loc->cam(old_v, new_v);
    if (is_indirect(old_v)) {
      versioned* val = loc->load();
      ver_link* old_l = (ver_link*) strip_indirect(old_v);
      if (val != old_l->value)
	; //link_pool.retire(old_l);
      else loc->cam(val, new_v);
    }
#endif
    return new_v;
  }

  void finalize_versioned_store(flck::atomic<versioned*>* loc, versioned* expected, TS time_stamp) {
    versioned* ptr_tagged = loc->load();
    if (ptr_tagged != expected) std::cout << "ouch: " << ptr_tagged << ", " << expected << std::endl;
    assert(ptr_tagged == expected);
    versioned* ptr = strip_indirect(ptr_tagged);
    assert(ptr != nullptr);
    // splice this latest version out if transaction aborted
    if (time_stamp == failed) {
      *loc = ptr->get_next_version();
      // no need to retire since will automatically be retired
      //if (is_indirect(ptr_tagged)) link_pool.retire((ver_link*) ptr);
    }
    // replace descriptor with timestamp (possibly failed stamp)
    ptr->remove_descriptor(time_stamp); 
  }

  template <typename T>
  T resolve_descriptor(trans_descriptor* descriptor, T old_v, T new_v,
		       bool validating) {
    if (descriptor->owner == flck::internal::current_id) {
      assert(validating); // self descriptor should not be installed if not validating
      return old_v; // if validating then should check against old value
    }
    TS desc_ts = descriptor->time_stamp.load();
    if (desc_ts == tbd || desc_ts == failed) return old_v;
#ifdef Validate
    if (validating && desc_ts == tbs &&
	descriptor->owner < flck::internal::current_id) { 
      // kill other if they have lower id. prevents validate cycle
      TS old = tbs;
      descriptor->time_stamp.compare_exchange_strong(old, failed);
    } else //otherwise wait until other is done (could also help)
      while (descriptor->time_stamp.load() == tbs);
    if (descriptor->time_stamp.load() == failed) return old_v;
#else
    descriptor->atomic_set_current_stamp();
#endif
    return new_v;
  }

  versioned* set_stamp(versioned* tagged_ptr, bool validating = false) {
    if (tagged_ptr == nullptr) return nullptr;
    versioned* ptr = strip_indirect(tagged_ptr);
    auto [descriptor, stamp] = ptr->get_descriptor_and_stamp();
    if (stamp == failed) return ptr->get_next_version();
    if (stamp != tbd) return tagged_ptr;
    if (stamp != tbd) return (stamp == failed) ? ptr->get_next_version() : tagged_ptr;
    if (descriptor == nullptr) {ptr->set_with_current_stamp(); return tagged_ptr; }
    return resolve_descriptor(descriptor, ptr->get_next_version(),
			      tagged_ptr, validating);
  }

  #include "atomic_val.h"
  
  template <typename V>
  struct versioned_ptr {
    //private:
    flck::atomic<versioned*> v;

    // probably a memory leak on retiring, need to look into it.
    // had to add the retire flag to prevent spurious double retires,
    // which probably happened on aborts
    void shortcut(versioned* ptr, bool retire) {
      assert(retire || (current_transaction != nullptr));
      ver_link* ptr_ = (ver_link*) strip_indirect(ptr);
      // need to fix for lock free locks
      if (ptr_->get_stamp_indirect() <= done_stamp) {
	assert(done_stamp < failed);
	if (v.cas(ptr, ptr_->value) && retire) link_pool.retire(ptr_);
	//if (v.cas(ptr, ptr_->value)) link_pool.retire(ptr_,false);
      }
    }

    static V* get_ptr(versioned* ptr) {
      versioned* ptr_ = strip_indirect(ptr);
      if (!is_indirect(ptr)) return (V*) ptr_;
      return (V*) ((ver_link*) ptr_)->value;
    }

    V* get_ptr_shortcut(versioned* ptr, bool retire=true) {
      versioned* ptr_ = strip_indirect(ptr);
      if (!is_indirect(ptr)) return (V*) ptr_;
#ifndef NoShortcut
      shortcut(ptr,retire);
#endif
      return (V*) ((ver_link*) ptr_)->value;
    }

    static versioned* set_zero_stamp(V* ptr) {
      if (ptr != nullptr && !ptr->is_recorded()) 
	ptr->set_stamp(zero_stamp);
      return ptr;
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

    std::optional<int> find_in_write_log() {
      for (int i=0; i < write_ptr_log.cnt; i++)
	if (write_ptr_log.vals[i].first == &v)
	  return std::optional<int>(i);
      return std::optional<int>();
    }
      
    V* load() {  
      assert(this != nullptr);
      if (local_stamp != -1) return read_snapshot();
      if (current_transaction != nullptr) {
	std::optional<int> lid = find_in_write_log();
	if (lid.has_value())
	  return (V*) write_ptr_log.vals[*lid].second;
	versioned* cur = set_stamp(v.load());
	if (in_lock) validate_ptr_log.add(std::tuple(&v, cur));
	return get_ptr_shortcut(cur, false);
	return get_ptr(cur);
      }
      return get_ptr_shortcut(set_stamp(v.load()));
    }

    void store(V* ptr) { 
      if (current_transaction != nullptr) {
	std::optional<int> lid = find_in_write_log();
	if (lid.has_value()) write_ptr_log.vals[*lid].second = ptr;
	else write_ptr_log.add(std::pair(&v,ptr));
      } else { // not in a transaction
	versioned* new_v = install_versioned_store(&v, ptr, nullptr);
	strip_indirect(new_v)->set_with_current_stamp();
      }
    }
    V* operator=(V* b) {store(b); return b; }

    bool validate(V* expected) {
      assert(this != nullptr);
      std::optional<int> lid = find_in_write_log();
      if (lid.has_value()) return (write_ptr_log.vals[*lid].second == expected);
      versioned* ptr = set_stamp(v.load());
      V* cur = (V*) (is_indirect(ptr) ? ((ver_link*) strip_indirect(ptr))->value : ptr);
      if (cur != expected) return false; 
      validate_ptr_log.add(std::tuple(&v, ptr));
      return true;
    }
  };

  struct Empty : versioned {};
  Empty true_val;
  
  struct atomic_bool {
    versioned_ptr<Empty> val;
    bool load() {return val.load() != nullptr;}
    void store(bool v) {val = v ? &true_val : nullptr;}
    atomic_bool(bool v) : val(v ? &true_val : nullptr) {}
    bool operator=(bool b) {store(b); return b; }
  };

#ifdef Validate
  template <typename T>
  bool validate_(versioned_ptr<T>* ptr, T* expected) {return ptr->validate(expected); }
  template <typename T>
  bool validate_(atomic<T>* ptr, T expected) {return ptr->validate(expected); }
  bool validate_(atomic_bool* value, bool expected) {
    Empty* cur = value->val.load();
    if ((cur == nullptr) == expected) return false;
    return value->val.validate(cur); }
  

  // bool validate_(flck::atomic_write_once<bool>* a, bool expected) {
  //   if (a->load() != expected) return false;
  //   validate_val_log.add(std::tuple(a, expected));
  //   return true;
  // }
#endif

  
  template <typename T, typename E>
  bool validate(verlib::lock& lck, T* v, E expected) {
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
