
namespace verlib {

  template <typename F>
  auto atomic_region(const F& func) {
    const unsigned int tid = ThreadRegistry::getTID();
    stm_tx_attr_t _a = {{.id = (unsigned int)tid, .read_only = false}};
    sigjmp_buf *_e = stm_start(_a);
    assert(_e != nullptr);
    sigsetjmp(*_e, 0);
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      func();
      stm_commit();
    } else {
      auto result = func();
      stm_commit();
      return result;
    }
  }

  template <typename F>
  auto atomic_read_only(const F& func) {
    const unsigned int tid = parlay::my_thread_id();
    stm_tx_attr_t _a = {{.id = (unsigned int)tid, .read_only = true}};
    sigjmp_buf *_e = stm_start(_a);
    assert(_e != nullptr);
    sigsetjmp(*_e, 0);
    auto retval = func();
    stm_commit();
    return retval;
  }

  struct versioned {};

  template <typename T>
  struct memory_pool {
    void reserve(size_t n) { }
    void stats() { }
    void clear() { }
    void Delete(T* ptr) { Retire(ptr); }
    void Retire(T* ptr) {
      stm_free(obj, sizeof(T)); }
    T* New(Args... args) {
      void* addr = stm_malloc(sizeof(T));
      assert(addr != NULL);
      new (addr) T(std::forward<Args>(args)...);
      return (T*) addr;
    }
    T* New_Init(F f, Args... args) {
      T* x = New(args...);
      f(x);
      return x;
    }
  };

  // locks are noops
  struct lock {
    template <typename Thunk>
    auto read_lock(Thunk f) { return f();}

    template <typename Thunk>
    auto try_lock(Thunk f) { return f();}
  };

  template <typename T>
  struct versioned_ptr {
  private:
    T* val;
  public:
    versioned_ptr(T* v) : val(v) {}
    versioned_ptr() : val(nullptr) {}
    void store(T* newVal) {
      stm_store((stm_word_t *)&val, (stm_word_t)newVal);
    }
    T* load() const {
      return (T*) stm_load((stm_word_t *)&val);
    }
    T* load_weak() { return load();}
    void validate() {  }
    T* read_snapshot() { return load(); }
    void init(T* x) { store(x);}
    T* operator=(T* b) {store(b); return b; }
  };

  struct atomic_bool {
  private:
    long v;
  public:
    atomic_bool(bool x) : v(x) {}
    void store(bool newVal) {
      stm_store((stm_word_t *) &v, (stm_word_t) newVal);
    }
    bool load() const {
      return (bool) stm_load((stm_word_t *) &v);
    }
    bool load_weak() { return load();}
    void validate() {  }
    T* operator=(bool b) {store(b); return b; }
  };
}
