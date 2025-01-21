#include <transactions/tlf.h>
#include <parlay/parallel.h>
#include "../utils.h"

// A concurrent tree with values stored at the leaves and no balancing
// Uses optimistic locking.

// Currently uses strict locks.
// Could use: std::unique_lock lck(p->lck, std::defer_lock);
//            if (!lck.try_lock()) return {};
// instead.

template <typename K_,
	  typename V_,
	  typename Less = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;

private:
  struct internal;
  internal* root;

  // Sentinal to mark smaller and larger than any key
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

  // Less and equal accounting for sentinals
  static bool less(const K& k, node* a) {
    if (a->s == IsKey) return Less{}(k, a->key);
    return (a->s == MaxSentinal);
  }
  
  static bool equal(const K& k, node* a) {
    return !(a->s != IsKey || Less{}(k, a->key) || Less{}(a->key ,k));
  }

  static tlf::atomic<node*>* get_child(internal* a, const K& k) {
    return less(k, a) ? &(a->left) : &(a->right);}

  // return a grandparent, parent and leaf
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

  static long size_rec(node* p) {
    if (p->is_leaf) return 1l;
    internal* pp = (internal*) p;
    size_t s1, s2;
    parlay::par_do([&] () { s1 = size_rec((pp->left).load());},
                   [&] () { s2 = size_rec((pp->right).load());});
    return s1 + s2;
  }

public:
  bool insert_(const K& k, const V& v) {
    while (true) {
      auto [gp, p, l] = find_location(root, k);
      auto ptr = get_child(p, k);
      if (equal(k, l)) return false;
      std::unique_lock lck_p(p->lck);
      if (p->removed.load() || ptr->load() != l)
        continue;
      node* new_l = tlf::New<leaf>(k, v);
      *ptr = (less(k, l) ?
              tlf::New<internal>(l->key, new_l, l) :
              tlf::New<internal>(k, l, new_l));
      return true;
    }
  }

  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=, this] {return insert_(k, v);}); }

  bool remove_(const K& k) {
    while (true) {
      auto [gp, p, l] = find_location(root, k);
      if (!equal(k, l)) return false;
      auto g_ptr = get_child(gp, k);
      auto p_ptr = get_child(p, k);
      std::unique_lock lck_gp(gp->lck);
      std::unique_lock lck_p(p->lck);
      if (!gp->removed.load() &&
          g_ptr->load() == p &&
          p_ptr->load() == l) {
        *g_ptr = less(k, p) ? (p->right).load() : (p->left).load();
        p->removed = true;
        tlf::Retire<internal>(p);
        tlf::Retire<leaf>(l);
        return true;
      }
    }
  }

  bool remove(const K& k) {
    return verlib::with_epoch([=, this] { return remove_(k);});}

  std::optional<V> find_(const K& k) {
    auto [gp, p, l] = find_location(root, k);
    if (equal(k, l)) return l->value; 
    else return {};
  }

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] { return find_(k);});}

  std::optional<V> find_locked(const K& k) {
    while (true) {
      auto [gp, p, l] = find_location(root, k);
      auto ptr = get_child(p, k);
      std::shared_lock lck_p(p->lck);
      if ((ptr->load() == (node*) l) && !p->removed.load()) {
        if (equal(k, l)) return l->value;
        else return {};
      }
    }
  }

  ordered_map() : root(tlf::New<internal>(tlf::New<leaf>())) {}
  ordered_map(size_t n) : root(tlf::New<internal>(tlf::New<leaf>())) {}
  ~ordered_map() { Retire(root);}
  
  void print() {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
	     if (p->s == MinSentinal) std::cout << "[";
	     else if (p->is_leaf) std::cout << p->key;
	     else {
	       internal* pp = (internal*) p;
	       prec((pp->left).load());
	       if (!((pp->left).load()->s == MaxSentinal))
		 std::cout << ", ";
	       prec((pp->right).load());
	     }
	   };
    prec(root->left.load());
    std::cout << "]" << std::endl;
  }

  static void Retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) tlf::Retire<leaf>((leaf*) p);
    else {
      internal* pp = (internal*) p;
      parlay::par_do([&] () { Retire((pp->left).load()); },
		     [&] () { Retire((pp->right).load()); });
      tlf::Retire<internal>(pp);
    }
  }
  
  // return total height (summed across leaves)
  long total_height() {
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


  long size() { return size_rec(root->left.load()) - 1; }

  // check properly ordered and sentinals are in the right place
  long check() {
    using rtup = std::tuple<K, K, long>;
    std::function<rtup(node*,bool)> crec;
    crec = [&] (node* p, bool leftmost) {
	     if (p->is_leaf) {
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
             }
	     if (p->s != IsKey) {
	       std::cout << "error, in leaftree interior node is a sentinal" << std::endl;
	       abort();
	     }
	     internal* pp = (internal*) p;
	     K lmin,lmax,rmin,rmax;
	     long lsum, rsum;
	     parlay::par_do([&] () { std::tie(lmin,lmax,lsum) = crec((pp->left).load(), leftmost);},
			    [&] () { std::tie(rmin,rmax,rsum) = crec((pp->right).load(), false);});
	     if ((lsum !=0 && !less(lmax, pp)) || less(rmin, pp))
	       std::cout << "error, in leaftree, out of order key" << std::endl;
	     if (lsum == 0) return rtup(pp->key, rmax, rsum);
	     else return rtup(lmin, rmax, lsum + rsum);
	   };
    auto l = (root->left).load();
    auto [minv, maxv, cnt] = crec(l, true);
    return cnt;
  }
  
  static void clear() {
    tlf::pool_clear<internal>();
    tlf::pool_clear<leaf>();
  }

  static void reserve(size_t n) {
    //internal_pool.reserve(n);
    //leaf_pool.reserve(n);
  }

  static void shuffle(size_t n) {
    //internal_pool.shuffle(n);
    //leaf_pool.shuffle(n);
  }

  static void print_stats() {
    tlf::pool_stats<internal>();
    tlf::pool_stats<leaf>();
  }
  
  static std::pair<size_t,size_t> stats() {
    auto p = tlf::stats<internal>();
    p += tlf::stats<leaf>();
    return p;
  }
};

