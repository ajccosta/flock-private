#include <verlib/verlib.h>
#include <bitset>

template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;
  static constexpr auto less = Compare{};

  static constexpr int max_levels = 24;
  static constexpr int log_sample_factor = 1;
  static constexpr int num_bits = max_levels * log_sample_factor;

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
  struct alignas(32) Node : verlib::versioned, verlib::lock {
    K key;
    V value;
    int level;
    verlib::atomic_bool removed;
    verlib::versioned_ptr<Node<0>> next[NumLevels];
    Node(const K& k, const V& v, int level)
      : key(k), value(v), level(level), removed(false) {}
    // for the root
    Node() : level(max_levels-1), removed(false) {
      for (int i=0; i < max_levels; i++)
        next[i] = nullptr;
    }
  };


  using tallNode = Node<max_levels>;
  using shortNode = Node<4>;
  using node = Node<0>;
  static verlib::memory_pool<tallNode> tall_node_pool;
  static verlib::memory_pool<shortNode> short_node_pool;
  node* new_node(const K& k, const V& v, int level) {
    if (level > 4) return (node*) tall_node_pool.New(k, v, level);
    else return (node*) short_node_pool.New(k, v, level);
  }
  void retire_node(node* v) {
    if (v->level > 4) tall_node_pool.Retire((tallNode*) v);
    else short_node_pool.Retire((shortNode*) v);
  }

  node* root;

  auto find_at_level(const K& k, int find_level) {
    node* cur = root;
    node* nxt = nullptr;
    int level = max_levels - 1;
    while (level >= find_level) {
      nxt = cur->next[level].load();
      if (nxt == nullptr || !less(nxt->key, k)) level--;
      else cur = nxt;
    }
    return std::make_pair(cur, nxt);
  }

  // Inserts a node at given level.
  // Works recursively by finding where to splice in and locking the previous node.
  // Splicing happens on way up recursion so it is done bottem up
  bool insert_at(node* cur, node* nxt, node* new_n, int level) {
    return cur->try_lock([=] {
      if (cur->removed.load() || cur->next[level].load() != nxt) return false;
      if (level > 0) {
        node* dcur = cur;
        node* dnext = cur->next[level - 1].load();
        while (dnext != nullptr && !less(new_n->key, dnext->key)) {
          dcur = dnext;
          dnext = dcur->next[level - 1].load();
        }
        if (!insert_at(dcur, dnext, new_n, level - 1))
          return false;
      }
      new_n->next[level] = cur->next[level].load();
      cur->next[level] = new_n;
      return true;});}
  
  bool insert_(const K& k, const V& v) {
    return flck::try_loop([&] () -> std::optional<bool> {
      int level = get_level(k);
      auto [cur, nxt] = find_at_level(k, level);
      if (!(nxt == nullptr) && !less(k, nxt->key)) return false;
      node* new_n = new_node(k, v, level);
      if (insert_at(cur, nxt, new_n, level)) return true;
      return std::optional<bool>();});
  }
  
  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  // Recursively locks prev node of to_delete on each level going down from level.
  // When reaching the bottom it goes through and splices out to_delete from each level
  bool remove_at(node* start, int start_level, node* prev, node* to_delete, int level) {
    return prev->try_lock([=] {
      if (prev->removed.load() || prev->next[level].load() != to_delete) 
        return false;
      if (level > 0) {
        node* dprev = prev;
        node* nxt;
        while ((nxt = dprev->next[level - 1].load()) != to_delete)
          dprev = nxt;
        return remove_at(start, start_level, dprev, to_delete, level - 1);
      }
      // when we reach the bottom, all prev nodes are locked
      // now, starting at the start level, going down, we splice out to_delete
      return to_delete->try_lock([=] {
               node* dprev = start;
               if (dprev->next[start_level].load() != to_delete) abort();
               dprev->next[start_level] = to_delete->next[start_level].load();
               for (int l = start_level-1; l >=0; l--) {
                 node* nxt;
                 while ((nxt = dprev->next[l].load()) != to_delete) // find where to splice out
                   dprev = nxt;
                 if (l == 0 && prev != dprev) abort();
                 // splice out
                 dprev->next[l] = to_delete->next[l].load();}
               to_delete->removed = true;
               return true;});});
  }

  bool remove_(const K& k) {
    return flck::try_loop([&] () -> std::optional<bool> {
       int level = get_level(k);
       auto [cur, nxt] = find_at_level(k, level);
       if ((nxt == nullptr) || less(k, nxt->key)) return false;
       if (remove_at(cur, level, cur, nxt, level)) {
         retire_node(nxt);
         return true;
       }
       else return std::optional<bool>();});
  }

  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  std::optional<V> find_locked(const K& k) {
    return find_(k);
    //return flck::try_loop([&] {return try_find(k);});
  }

  // std::optional<std::optional<V>> try_find(const K& k) {
  //   using ot = std::optional<std::optional<V>>;
  //   auto [cur, nxt] = find_location(root, k);
  //   if (cur->lck.read_lock([&] {return (cur->next.load() == nxt) && !cur->removed.load();}))
  //     return ot(!nxt->is_end && !less(k, nxt->key) ?
  //       	std::optional<V>(nxt->value) :
  //       	std::optional<V>());
  //   else return ot();
  // }

  std::optional<V> find_(const K& k) {
    auto [cur, nxt] = find_at_level(k, 0);
    if  (nxt != nullptr && !less(k, nxt->key)) return nxt->value;
    else return {};
  }

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
    while (p != nullptr) {
      node* tmp = p;
      p = p->next[0].load();
      retire_node(tmp);
    }
  }
  
  long check() {
    node* ptr = (root->next[0]).load();
    if (ptr == nullptr) return 0;
    K k = ptr->key;
    ptr = (ptr->next[0]).load();
    long i = 1;
    while (ptr != nullptr) {
      i++;
      if (!less(k, ptr->key)) {
        std::cout << "keys out of order: " << k << ", " << ptr->key << " at location " << i << std::endl;
        abort();
      }
      k = ptr->key;
      ptr = (ptr->next[0]).load();
    }
    return i;
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
