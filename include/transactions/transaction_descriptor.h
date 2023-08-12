namespace verlib {
  using atomic_val = size_t;
  thread_local int transaction_round = 0;
  
  // structure for holding the various logs
  // has a default size and will expand if needed
  template <typename T, int InitLength=32>
  struct transaction_log {
  private:
    static constexpr int init_log_length = InitLength;
    std::array<T,init_log_length> vals;
    parlay::sequence<T> extra_vals;
    int cnt;
  public:
    int size() {return cnt;}
    void add(T v) {
      if (cnt < init_log_length) {vals[cnt++] = v; return;}
      if (cnt == init_log_length) {
	extra_vals.reserve(2*init_log_length);
	for (int i=0; i < init_log_length; i++)
	  extra_vals.push_back(vals[i]);
      }
      extra_vals.push_back(v);
      cnt++;
    }
    T* data() {
      return (cnt > init_log_length) ? extra_vals.data() : &vals[0];}
    void clear() { cnt = 0; extra_vals.clear();}
    T& operator [](int i) {
      return (cnt > init_log_length) ? extra_vals[i] : vals[i];}
    transaction_log() : cnt(0) {}
  };

  // Special timestamps
  const TS tbd = std::numeric_limits<TS>::max()/4;
  const TS tbs = std::numeric_limits<TS>::max()/4 - 1;
  const TS failed = std::numeric_limits<TS>::max()/4 - 2;

  // needed for forward reference
  struct versioned;

  // structure that stores transaction information, including logs and state
  struct trans_descriptor {
    // originally "tbd", then "tbs", then either a real timestamp or "failed"
    flck::atomic_aba_free<TS> time_stamp;  
    static constexpr int init_log_size = 8;
    
    std::atomic<bool> acquired;   

    // the thread id the descriptor belongs to
    const int owner;
    const int round;  // for debugging

    transaction_log<std::pair<flck::atomic<versioned*>*,versioned*>,
		    init_log_size> write_ptr_log;
    transaction_log<std::tuple<flck::atomic<versioned*>*,versioned*>,
		    4*init_log_size> validate_ptr_log;
    transaction_log<bool*,init_log_size> retired_log;
    transaction_log<bool*,init_log_size> allocated_log;
    transaction_log<flck::lock*,init_log_size> lock_log;
    
    void atomic_set_stamp(TS ts) {
      assert(time_stamp.load() != tbd);
      if (time_stamp.load() == tbs) {
	TS old = tbs;
	time_stamp.cam(old, ts);
      }
    }

    void atomic_set_current_stamp() {
      if (time_stamp.load() == tbs)
	atomic_set_stamp(global_stamp.get_write_stamp());
    }

    //trans_descriptor() : time_stamp(tbd), owner(flck::internal::current_id) {
    trans_descriptor() : time_stamp(tbd), owner(parlay::worker_id()), round(++transaction_round) {
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }
  };
  thread_local trans_descriptor* current_transaction = nullptr;

#ifdef NoHelp
  flck::internal::acquired_pool<trans_descriptor> trans_descriptor_pool;
#else
  flck::internal::acquired_pool<trans_descriptor> trans_descriptor_pool;
  //flck::memory_pool<trans_descriptor> trans_descriptor_pool;
#endif
}
