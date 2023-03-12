namespace verlib {
  template <typename xT>
  struct memory_pool {
    using T = xT;
    flck::memory_pool<T> pool;
    void reserve(size_t n) { pool.reserve(n);}
    void shuffle(size_t n) { pool.shuffle(n);}
    void stats() { pool.stats();}
    void clear() { pool.clear();}
    void destruct(T* ptr) { pool.destruct(ptr); }

    template <typename ... Args>
    T* new_obj(Args... args) {
#ifdef TransUndo
      return new_obj_descriptor(verlib::current_transaction, args...);
#else
      return pool.new_obj(args...);
#endif
    }

    template <typename ... Args>
    T* new_obj_descriptor(trans_descriptor* descriptor, Args... args) {
      T* ptr = pool.new_obj(args...);
#ifdef TransUndo
      if (descriptor != nullptr)  {
	bool* x = pool.retire(ptr);
	flck::internal::undo_retire(x);
	descriptor->allocated_log.add(x);
      }
#endif
      return ptr;
    }

    template <typename F, typename ... Args>
    T* new_init(F f, Args... args) {
      T* x = new_obj(args...);
      f(x);
      return x;
    }

    void retire(T* ptr) {
#ifdef TransUndo
      retire_descriptor(current_transaction, ptr);
#else
      pool.retire(ptr);
#endif
    }
  
    void retire_descriptor(trans_descriptor* descriptor, T* ptr) {
      bool* x = pool.retire(ptr);
      if (descriptor != nullptr) descriptor->retired_log.add(x);
    }
  };

}
