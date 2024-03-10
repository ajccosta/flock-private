#ifndef TLF_MEMORY_POOL_H_
#define TLF_MEMORY_POOL_H_

namespace verlib {
  template <typename xT>
  struct memory_pool {
    using T = xT;
    flck::memory_pool<T> pool;
    void reserve(size_t n) { pool.reserve(n);}
    void shuffle(size_t n) { pool.shuffle(n);}
    void stats() { pool.stats();}
    void clear() { pool.clear();}
    void Delete(T* ptr) { pool.Delete(ptr); }

    bool is_helping() {
#ifdef NoHelp
      return false;
#else
      return flck::internal::helping;
#endif
    }

    void retire_on_abort(transaction_descriptor* descriptor, T* ptr) {
      if (descriptor != nullptr && !is_helping()) {
	bool* x = pool.Retire(ptr);
	epoch::internal::undo_Retire(x);
	descriptor->allocated_log.add(x);
      }
    }

    void retire_on_success(transaction_descriptor* descriptor, T* ptr) {
      bool* x = pool.Retire(ptr);
      if (descriptor != nullptr && !is_helping())
	descriptor->retired_log.add(x);
    }

    bool check_not_corrupted(T* ptr) {
      return pool.check_not_corrupted(ptr);
    }

    template <typename ... Args>
    T* New(Args... args) {
      T* ptr = pool.New(args...);
      retire_on_abort(current_transaction, ptr);
      return ptr;
    }

    template <typename F, typename ... Args>
    T* new_init(F f, Args... args) {
      T* ptr = pool.new_init(f, args...);
      retire_on_abort(current_transaction, ptr);
      return ptr;
    }

    void Retire(T* ptr) {
      retire_on_success(current_transaction, ptr);
    }
  };
}

#endif // TLF_MEMORY_POOL_H_
