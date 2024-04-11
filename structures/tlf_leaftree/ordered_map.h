#include <transactions/tlf.h>
#include <parlay/parallel.h>

template <typename K_,
	  typename V_,
	  typename Compare = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;

  struct internal;
  internal* root;
  enum sentinal : char {MinSentinal, IsKey, MaxSentinal};
  
  // common header for internal nodes and leaves
  struct node : verlib::versioned {
    K key;
    bool is_leaf;
    sentinal s;
    node(K k, bool l, sentinal s) : key(k), is_leaf(l), s(s) {}
  };
  
  // internal node
  struct internal : node {
    tlf::atomic<bool> removed;
    tlf::shared_mutex lck;
    tlf::atomic<node*> left;
    tlf::atomic<node*> right;
    internal(K k, node* left, node* right)
      : node{k, false, IsKey}, left(left), right(right), removed(false) {};
    internal(node* left) // for the root, only has a left pointer
      : node{K{}, false, MaxSentinal}, left(left), right(nullptr), removed(false) {};
  };

  struct leaf : node {
    V value;
    leaf(K k, V v) : node{k, true, IsKey}, value(v) {};
    leaf() : node{K{}, true, MinSentinal} {}; // for the sentinal leaf
  };

  static bool Less(const K& k, node* a) {
    if (a->s == IsKey) return Compare{}(k, a->key);
    return (a->s == MaxSentinal);
  }
  
  static bool Equal(const K& k, node* a) {
    return !(a->s != IsKey || Compare{}(k, a->key) || Compare{}(a->key ,k));
  }

  static verlib::memory_pool<internal> internal_pool;
  static verlib::memory_pool<leaf> leaf_pool;

  static tlf::atomic<node*>* get_child(internal* a, const K& k) {
    return Less(k, a) ? &(a->left) : &(a->right);}

  static auto find_location(internal* root, const K& k) {
    internal* gp = nullptr;
    internal* p = root;
    node* l = (p->left).load();
    while (!l->is_leaf) {
      gp = p;
      p = (internal*) l;
      l = get_child(p, k)->load();
    }
    return std::make_tuple(gp, p, (leaf*) l);
  }

  bool insert_(const K& k, const V& v) {
    while (true) {
      auto [gp, p, l] = find_location(root, k);
      auto ptr = get_child(p, k);
      if (Equal(k, l)) return false;
      std::unique_lock lck_p(p->lck);
      //std::unique_lock lck(p->lck, std::defer_lock);
      //if (!lck.try_lock()) return {};
      if (p->removed.load() || ptr->load() != l) continue;
      node* new_l = leaf_pool.New(k, v);
      *ptr = (Less(k, l) ?
              internal_pool.New(l->key, new_l, l) :
              internal_pool.New(k, l, new_l));
      return true;
    }
  }

  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  bool remove_(const K& k) {
    while (true) {
      auto [gp, p, l] = find_location(root, k);
      if (!Equal(k, l)) return false;
      auto g_ptr = get_child(gp, k);
      auto p_ptr = get_child(p, k);
      std::unique_lock lck_gp(gp->lck);
      std::unique_lock lck_p(p->lck);
      if (!gp->removed.load() &&
          g_ptr->load() == p &&
          p_ptr->load() == l) {
        *g_ptr = Less(k, p) ? (p->right).load() : (p->left).load();
        p->removed = true;
        internal_pool.Retire(p);
        leaf_pool.Retire(l);
        return true;
      }
    }
  }

  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  std::optional<V> find_(const K& k) {
    auto [gp, p, l] = find_location(root, k);
    if (Equal(k, l)) return l->value; 
    else return {};
  }

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] { return find_(k);});}

  std::optional<V> find_locked(const K& k) {
    while (true) {
      auto [gp, p, l] = find_location(root, k);
      auto ptr = get_child(p, k);
      std::shared_lock lck_p(p->lck);
      if ((ptr->load() == (node*) l) && !p->removed.load())
        if (Equal(k, l)) return l->value;
        else return {};
    }
  }

  ordered_map() : root(internal_pool.New(leaf_pool.New())) {}
  ordered_map(size_t n) : root(internal_pool.New(leaf_pool.New())) {}
  ~ordered_map() { Retire(root);}
  
  void print() {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
	     if (p->s == MinSentinal) std::cout << "[";
	     else if (p->is_leaf) std::cout << p->key;
	     else {
	       internal* pp = (internal*) p;
	       prec((pp->left).load());
	       if (!(pp->left).load()->s == MaxSentinal)
		 std::cout << ", ";
	       prec((pp->right).load());
	     }
	   };
    prec(root->left.load());
    std::cout << "]" << std::endl;
  }

  static void Retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) leaf_pool.Retire((leaf*) p);
    else {
      internal* pp = (internal*) p;
      parlay::par_do([&] () { Retire((pp->left).load()); },
		     [&] () { Retire((pp->right).load()); });
      internal_pool.Retire(pp);
    }
  }
  
  // return total height
  double total_height() {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
	     if (p->is_leaf) return depth;
	     internal* pp = (internal*) p;
	     size_t d1, d2;
	     parlay::par_do([&] () { d1 = hrec((pp->left).load(), depth + 1);},
			    [&] () { d2 = hrec((pp->right).load(), depth + 1);});
	     return d1 + d2;
	   };
    return hrec(root->left.load(), 1);
  }

  long check() {
    using rtup = std::tuple<K, K, long>;
    std::function<rtup(node*,bool)> crec;
    crec = [&] (node* p, bool leftmost) {
	     if (p->is_leaf)
	       if (leftmost) {
		 if (p->s == MinSentinal) return rtup(p->key, p->key, 0);
		 else {
		   std::cout << "error, in leaftree leftmost leaf is not a sentinal" << std::endl;
		   abort();
		 }
	       } else if (p->s == MinSentinal) {
		 std::cout << "error, in leaftree not leftmost leaf is a sentinal" << std::endl;
		 abort();
	       } else return rtup(p->key, p->key, 1);
	     if (p->s != IsKey) {
	       std::cout << "error, in leaftree interior node is a sentinal" << std::endl;
	       abort();
	     }
	     internal* pp = (internal*) p;
	     K lmin,lmax,rmin,rmax;
	     long lsum, rsum;
	     parlay::par_do([&] () { std::tie(lmin,lmax,lsum) = crec((pp->left).load(), leftmost);},
			    [&] () { std::tie(rmin,rmax,rsum) = crec((pp->right).load(), false);});
	     if ((lsum !=0 && !Less(lmax, pp)) || Less(rmin, pp))
	       std::cout << "error, in leaftree, out of order key" << std::endl;
	     if (lsum == 0) return rtup(pp->key, rmax, rsum);
	     else return rtup(lmin, rmax, lsum + rsum);
	   };
    auto l = (root->left).load();
    auto [minv, maxv, cnt] = crec(l, true);
    return cnt;
  }
  
  static void clear() {
    internal_pool.clear();
    leaf_pool.clear();
  }

  static void reserve(size_t n) {
    internal_pool.reserve(n);
    leaf_pool.reserve(n);
  }

  static void shuffle(size_t n) {
    internal_pool.shuffle(n);
    leaf_pool.shuffle(n);
  }

  static void stats() {
    internal_pool.stats();
    leaf_pool.stats();
  }
  
};

template <typename K, typename V, typename C>
verlib::memory_pool<typename ordered_map<K,V,C>::internal> ordered_map<K,V,C>::internal_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename ordered_map<K,V,C>::leaf> ordered_map<K,V,C>::leaf_pool;
