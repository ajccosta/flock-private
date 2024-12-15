#include <verlib/verlib.h>


template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;
  static constexpr auto less = Compare{};

  static constexpr int max_levels = 20;
  static constexpr int log_sample_factor = 1;
  static constexpr int num_bits = max_levels * log_sample_factor;

  // the level of a key is based on its hash
  int get_level(const K& key) {
    size_t mask = (1 << (num_bits - 1)) - 1;
    int keep_mask = (1 << log_sample_factor) - 1;
    size_t h = parlay::hash<K>{}(key) & mask;
    int level = 0;
    size_t hh = h;
    while ((h & keep_mask) == keep_mask) {
      level++;
      h = h >> log_sample_factor;
    }
    return level;
  }

  template <int NumLevels>
  struct alignas(32) Node : verlib::versioned {
    K key;
    V value;
    int level;
    verlib::atomic_bool_ removed;
    verlib::versioned_ptr<Node<0>> next[NumLevels+1];
    Node(const K& k, const V& v, int level)
      : key(k), value(v), level(level), removed(false) {}
    // for the root
    Node() : level(max_levels-1), removed(false) {
      for (int i=0; i < max_levels; i++)
        next[i] = nullptr;
    }
  };

  using node = Node<0>;

  // Memory management
  // two types of node to save space
  static constexpr int small_cutoff = 4;
  using tallNode = Node<max_levels>;
  using shortNode = Node<small_cutoff>;
  static verlib::memory_pool<tallNode> tall_node_pool;
  static verlib::memory_pool<shortNode> short_node_pool;
  
  node* new_node(const K& k, const V& v, int level) {
    if (level > small_cutoff) return (node*) tall_node_pool.New(k, v, level);
    else return (node*) short_node_pool.New(k, v, level);
  }
  void retire_node(node* v) {
    if (v->level > small_cutoff) tall_node_pool.Retire((tallNode*) v);
    else short_node_pool.Retire((shortNode*) v);
  }
  
  node* root;

  // find the node before and after the key (or at if in there) on the given level
  auto find_at_level(const K& k, int find_level) {
    node* prev = root;
    node* nxt = nullptr;
    int level = max_levels - 1;
    while (level >= find_level) {
      nxt = prev->next[level].load_weak();
      if (nxt == nullptr || !less(nxt->key, k)) level--;
      else prev = nxt;
    }
    prev->next[find_level].load();
    prev->removed.load();
    return std::make_pair(prev, nxt);
  }

  
  // insert at each level at or below "level"
  void insert_at(node* cur, node* nxt, node* new_node, int level) {
    while (true) {
      new_node->next[level] = cur->next[level].load();
      cur->next[level] = new_node;
      if (level-- == 0) return;
      node* next = cur->next[level].load();
      while (next != nullptr && !less(new_node->key, next->key)) {
        cur = next;
        next = cur->next[level].load();
      }
    }
  }
  
  bool insert_(const K& k, const V& v) {
    int level = get_level(k);
    auto [cur, nxt] = find_at_level(k, level);
    if (!(nxt == nullptr) && !less(k, nxt->key))
      return false;
    insert_at(cur, nxt, new_node(k, v, level), level);
    return true;
  }
  
  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=, this] {return insert_(k, v);}); }

  bool upsert_(const K& k, const V& v) {
    // int level = get_level(k);
    // auto [cur, nxt] = find_at_level(k, level);
    // if (!(nxt == nullptr) && !less(k, nxt->key)) { // found
    //   Val* oldv = nxt->value.load();
    //   nxt->value = val_pool.New(v); // update the value
    //   val_pool.Retire(oldv);
    //   return false;
    // }
    // // not found
    // node* new_n = new_node(k, v, level);
    // //if (insert_at_level(cur, nxt, new_n, level))
    // //  return true;
    return true;
  }

  bool upsert(const K& k, const V& v) {
    return verlib::with_epoch([=, this] {return upsert_(k, v);}); }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    return upsert_(k,f(find_(k)));
  }

  bool remove_(const K& k) {
    int level = get_level(k);
    auto [prev, to_delete] = find_at_level(k, level);
    // if not found, then return false
    if ((to_delete == nullptr) || less(k, to_delete->key))
      return false;
    prev->next[level] = to_delete->next[level].load();
    while (--level >= 0) {
      node* tmp;
      while ((tmp = prev->next[level].load()) != to_delete) // find where to splice out
        prev = tmp;
      prev->next[level] = to_delete->next[level].load();
    }
    to_delete->removed = true;
    retire_node(to_delete);
    return true;
  }

  bool remove(const K& k) {
    return verlib::with_epoch([=, this] { return remove_(k);});}

  std::optional<V> find_(const K& k) {
    auto [cur, nxt] = find_at_level(k, 0);
    if  (nxt != nullptr && !less(k, nxt->key)) return nxt->value;
    else return {};
  }

  std::optional<V> find_locked(const K& k) { return find_(k);}

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] {return find_(k);});
  }

  ordered_map() : root((node*) tall_node_pool.New()) {}
  ordered_map(long n) : root((node*) tall_node_pool.New()) {}

  void print() {
    node* ptr = (root->next[0]).load();
    while (ptr != nullptr) {
      std::cout << ptr->key << ", ";
      ptr = (ptr->next[0]).load();
    }
    std::cout << std::endl;
  }

  ~ordered_map() {
    node* p = root;
    std::vector<node*> tops;
    while (p != nullptr) {
      tops.push_back(p);
      p = p->next[10].load();
    }
    parlay::parallel_for(0, tops.size(), [&] (long i) {
      node* p = tops[i];
      do {
        node* tmp = p;
        p = p->next[0].load();
        retire_node(tmp);
      } while (p != nullptr && p->level < 10);});
  }

  long check() {
    node* p = root;
    std::vector<node*> tops;
    while (p != nullptr) {
      tops.push_back(p);
      p = p->next[10].load();
    }
    return parlay::reduce(parlay::tabulate(tops.size(), [&] (long i) {
      node* p = tops[i];
      node* nxt = p->next[0].load();
      long cnt = 0;
      while (nxt != nullptr && nxt->level < 10) {
	if (!(i == 0 && cnt == 0) && !less(p->key, nxt->key)) {
	  std::cout << "in skiplist: keys out of order";
	  abort();
	}
	p = nxt;
	nxt = p->next[0].load();
	cnt++;
      }
      if (!(i == 0 && cnt == 0) && nxt != nullptr && !less(p->key, nxt->key)) {
	std::cout << "in skiplist: keys out of order" << std::endl;
	abort();
      }
      return cnt+1;})) - 1;
  }

  static void clear() { tall_node_pool.clear(); short_node_pool.clear();}
  static void reserve(size_t n) { }
  static void shuffle(size_t n) { }
  static void stats() { tall_node_pool.stats(); short_node_pool.stats();}

};

template <typename K, typename V, typename C>
verlib::memory_pool<typename ordered_map<K,V,C>::shortNode> ordered_map<K,V,C>::short_node_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename ordered_map<K,V,C>::tallNode> ordered_map<K,V,C>::tall_node_pool;
