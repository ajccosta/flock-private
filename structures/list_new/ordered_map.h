#include <verlib/verlib.h>

template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_list {
  using K = K_;
  using V = V_;
  static constexpr auto less = Compare{};

  struct node;
  
  struct locked_ptr : verlib::lock {
    verlib::versioned_ptr<node> ptr;
    locked_ptr(node* p) : ptr(p) {}
  };

  struct alignas(32) node : verlib::versioned {
    locked_ptr next;
    K key;
    V value;
    verlib::atomic_bool removed;
    node(K key, V value, node* next)
      : key(key), value(value), next(next) {};
  };

  locked_ptr root;
  
  static verlib::memory_pool<node> node_pool;

  auto find_location(const K& k) {
    locked_ptr* cur = &root;
    verlib::atomic_bool* removed_p = nullptr;
    node* nxt = cur->ptr.load();
    while (true) {
      if (nxt == nullptr || !less(nxt->key, k)) break;
      cur = &nxt->next;
      auto h = nxt;
      nxt = cur->ptr.load();
      __builtin_prefetch (nxt);
      removed_p = &h->removed;
    }
    return std::make_tuple(removed_p, cur, nxt);
  }

  std::optional<bool> try_insert(const K& k, const V& v) {
    auto [rp, cur, nxt] = find_location(k);
    if (nxt != nullptr && !less(k, nxt->key)) //already there
      if (cur->read_lock([&] {
          return (cur->ptr.load() == nxt) && (rp == nullptr || !rp->load());}))
	return false;
      else return {};
    if (cur->try_lock([=] {
      if ((cur->ptr.load() == nxt) && (rp == nullptr || !rp->load())) {
	node* new_node = node_pool.New(k, v, nxt);
	cur->ptr.store(new_node); // splice in
	return true;
      } else return false;})) return true;
    else return {}; // failed
  }

  bool insert_(const K& k, const V& v) {
    return flck::try_loop([&] {return try_insert(k, v);});}
  
  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  std::optional<bool> try_remove(const K& k) {
    auto [rp, cur, nxt] = find_location(k);
    if (nxt == nullptr || less(k, nxt->key)) // not found
      if (cur->read_lock([&] {
	  return (cur->ptr.load() == nxt) && (rp == nullptr || !rp->load());}))
	return false;
      else return {};
    if (cur->try_lock([=] {
      if ((cur->ptr.load() == nxt) && (rp == nullptr || !rp->load())) {
	return nxt->next.try_lock([=] {
          node* nxtnxt = (nxt->next).ptr.load();
	  nxt->removed = true;
	  cur->ptr.store(nxtnxt); // shortcut
	  node_pool.Retire(nxt);
	  return true;});
      } else return false;})) return true;
    else return {};
  }

  bool remove_(const K& k) {
    return flck::try_loop([&] {return try_remove(k);});}
  
  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  std::optional<V> find_locked(const K& k) {
    return flck::try_loop([&] {return try_find(k);});
  }

  std::optional<std::optional<V>> try_find(const K& k) {
    using ot = std::optional<std::optional<V>>;
    auto [rp, cur, nxt] = find_location(k);
    if (cur->read_lock([&] {
        return (cur->ptr.load() == nxt) && (rp == nullptr || !rp->load());}))
      return ot(!nxt != nullptr && !less(k, nxt->key) ?
		std::optional<V>(nxt->value) :
		std::optional<V>());
    else return ot();
  }

  std::optional<V> find_(const K& k) {
    auto [dp, cur, nxt] = find_location(k);
    if (nxt != nullptr && !less(k, nxt->key)) return nxt->value;
    else return {};
  }

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] {return find_(k);});
  }

  template<typename AddF>
  void range_(AddF& add, const K& start, const K& end) {}

  ordered_list() : root(nullptr) {}
  ordered_list(size_t n) : root(nullptr) {}

  void print() {}

  void retire_recursive(node* p) {
    if (p != nullptr) {
      retire_recursive(p->next.ptr.load());
      node_pool.Retire(p);
    }
  }

  ~ordered_list() {retire_recursive(root.ptr.load());}
  
  long check() {
    node* ptr = root.ptr.load();
    if (ptr == nullptr) return 0;
    K k = ptr->key;
    ptr = ptr->next.ptr.load();
    long i = 1;
    while (ptr != nullptr) {
      i++;
      if (!less(k, ptr->key)) {
        std::cout << "bad key: " << k << ", " << ptr->key << std::endl;
        abort();
      }
      k = ptr->key;
      ptr = ptr->next.ptr.load();
    }
    return i;
  }

  long size() {
    node* ptr = root.ptr.load();
    long r = 0;
    while (ptr != nullptr) {
      r++;
      ptr = ptr->next.ptr.load();
    }
    return r;
  }

  static void clear() { node_pool.clear();}
  static void reserve(size_t n) { node_pool.reserve(n);}
  static void shuffle(size_t n) { node_pool.shuffle(n);}
  static void stats() { node_pool.stats();}

};

template <typename K, typename V, typename C>
verlib::memory_pool<typename ordered_list<K,V,C>::node> ordered_list<K,V,C>::node_pool;
