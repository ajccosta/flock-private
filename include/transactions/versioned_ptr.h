// Definition of versioned_ptr

namespace verlib {

  template <typename V>
  struct versioned_ptr {
  private:
    flck::atomic<versioned*> v;

    V* get_ptr_shortcut(versioned* ptr) {
      versioned* ptr_ = strip_indirect(ptr);
      if (!is_indirect(ptr)) return (V*) ptr_;
#ifndef NoShortcut
      shortcut(&v, ptr);
#endif
      return (V*) ((ver_link*) ptr_)->value;
    }

    static versioned* set_zero_stamp(V* ptr) {
      if (ptr != nullptr) ptr->set_zero_stamp();
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
      versioned* ptr = get_value(v.read());
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
      auto& log = current_transaction->write_ptr_log;
      for (int i=0; i < log.size(); i++)
	if (log[i].first == &v)
	  return std::optional<int>(i);
      return std::optional<int>();
    }

    // should be private (only used by load_transactional)
    // Returns the value of the version of "ptr" at time stamp "at_stamp".
    // If the most recent version is ahead of at_stamp, and in_lock is
    // set, then sets "old_version_read" to true.
    V* get_version(versioned* ptr, TS at_stamp) {
#ifndef NoShortcut
      if (is_indirect(ptr)) shortcut(&v, ptr);
#endif
      versioned* ptr_ = strip_indirect(ptr);
      if (ptr != nullptr && ptr_->get_stamp() > at_stamp) {
	if (in_lock) old_version_read = true;
	// chase down version chain
	do {
	  ptr = ptr_->get_next_version();
	  ptr_ = strip_indirect(ptr);
	} while (ptr != nullptr && ptr_->get_stamp() > at_stamp);
      }
      if (!is_indirect(ptr)) return (V*) ptr;
      else return (V*) ((ver_link*) ptr_)->value;
    }


    V* load() {  
      assert(!is_helping() || current_transaction == nullptr);
      if (local_stamp != -1)  return read_snapshot();
      if (current_transaction == nullptr) {
	versioned* ptr = v.load();
	// skip get_value in common case
	if (ptr != nullptr && !strip_indirect(ptr)->simple_stamp())
	  ptr = get_value(ptr);
	return get_ptr_shortcut(ptr);
      } else return load_transactional();
    }

    // should be private (only used by load)
    V* load_transactional() {
      // first see if it is in the write log
      std::optional<int> lid = find_in_write_log();
      if (lid.has_value())
	return (V*) current_transaction->write_ptr_log[*lid].second;

      // if not then get the value and add to validate log if in a lock
      versioned* ptr = get_value(v.load());
      if (in_lock) 
	current_transaction->validate_ptr_log.add(std::tuple(&v, ptr));

      // returns version at start_stamp for opacity
      return get_version(ptr, start_stamp);
    }

    void store(V* ptr) { 
      if (current_transaction != nullptr) {
	// check if it is in the write log: update if so, otherwise add to write log
	std::optional<int> lid = find_in_write_log();
	if (lid.has_value()) current_transaction->write_ptr_log[*lid].second = ptr;
	else current_transaction->write_ptr_log.add(std::pair(&v,ptr));
      } else { // not in a transaction
	install_store(&v, ptr, nullptr);
      }
    }
    V* operator=(V* b) {store(b); return b; }

    bool validate(V* expected) {
      // check if in write log.  if so no need to validate
      std::optional<int> lid = find_in_write_log();
      if (lid.has_value())
	return (current_transaction->write_ptr_log[*lid].second == expected);

      // otherwise check that value matches expected, and if so add to validate log
      versioned* ptr = get_value(v.load());
      //V* cur = (V*) (is_indirect(ptr) ? ((ver_link*) strip_indirect(ptr))->value : ptr);
      //if (cur != expected) return false;
      // Note that adding "ptr" not "expected" to log. This is because
      // ptr might be an indirect pointer to the expected value and
      // the pointers need to match to avoid ABA problems.
      current_transaction->validate_ptr_log.add(std::tuple(&v, ptr));
      return true;
    }
  };

  struct Empty : versioned {
    Empty() {versioned::set_zero_stamp();}
  };
  Empty true_struct;
  Empty* false_val = nullptr;
  Empty* true_val = &true_struct;
  
  // Simulates a boolean with a pointer to an "Empty" dummy struct.
  // If false, then pointer is nullptr otherwise pointer to "true_struct".
  // Avoids level of indirection when storing false.
  struct atomic_bool {
    versioned_ptr<Empty> val;
    bool load() {return val.load() == true_val;}
    void store(bool v) {val = (v ? true_val : false_val);}
    bool validate(bool expected) {
      Empty* cur = val.load();
      if ((cur == false_val) == expected) return false;
      return val.validate(cur); }
    atomic_bool(bool v) : val(v ? true_val : false_val) {}
    bool operator=(bool b) {store(b); return b; }
  };

  // should probably go into the lock code.
  template <typename T, typename E>
  bool validate(verlib::lock& lck, T* v, E expected) {
    if (current_transaction != nullptr) {
#ifdef Validate
      if (true) //try_count < 100) // anywhere from 10-100 seems good
	return v->validate(expected);
      else // if too many tries give up on validation and take locks
	return lck.try_lock([=] {return (v->load() == expected);});
#else
      return lck.try_lock([=] {return (v->load() == expected);});
#endif
    } else return true;
  }

} // namespace verlib
