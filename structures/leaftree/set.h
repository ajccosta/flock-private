#include <verlib/verlib.h>

template <typename K,
	  typename V,
	  typename Compare = std::less<K>>
struct Set {
  static auto less = Compare{};
  static bool equal(K a, K b) {
    return !less(a,b) && !less(b,a);
  }

  // common header for internal nodes and leaves
  struct node : verlib::versioned {
    K key;
    bool is_leaf;
    bool is_sentinal;
    flck::atomic_write_once<bool> removed; // not used for leaves, but fits here
    node(K key, bool is_leaf)
      : key(key), is_leaf(is_leaf), is_sentinal(false), removed(false) {}
    node(bool is_sentinal) :
      is_leaf(is_sentinal), is_sentinal(is_sentinal), removed(false) {}
  };

  // internal node
  struct internal : node, flck::lock {
    verlib::versioned_ptr<node> left;
    verlib::versioned_ptr<node> right;
    internal(K k, node* left, node* right)
      : node{k,false}, left(left), right(right) {};
    internal(node* left) // for the root, only has a left pointer
      : node{false}, left(left), right(nullptr) {};
  };

  struct leaf : node {
    V value;
    leaf(K k, V v) : node{k,true}, value(v) {};
    leaf() : node{true} {}; // for the sentinal leaf
  };

  verlib::memory_pool<internal> internal_pool;
  verlib::memory_pool<leaf> leaf_pool;

  auto find_location(internal* root, K k) {
    internal* gp = nullptr;
    bool gp_left = false;
    internal* p = root;
    bool p_left = true;
    node* l = (p->left).load();
    while (!l->is_leaf) {
      gp = p;
      gp_left = p_left;
      p = (internal*) l;
      p_left = less(k, p->key);
      l = p_left ? (p->left).load() : (p->right).load();
    }
    return std::make_tuple(gp, gp_left, p, p_left, (leaf*) l);
  }

  bool insert(internal* root, K k, V v, bool upsert=false) {
    return verlib::with_epoch([=] {
      leaf* prev_leaf = nullptr;
      while (true) {
	auto [gp, gp_left, p, p_left, l] = find_location(root, k);
	if (!l->is_sentinal &&
	    ((!upsert && equal(l->key, k)) ||
	     (upsert && prev_leaf != nullptr && prev_leaf->key == k &&
	      l != prev_leaf)))
	  return false;
	prev_leaf = l;
	auto r = p->try_lock([=] {
	      auto ptr = p_left ? &(p->left) : &(p->right);
	      auto l_new = ptr->load();
	      if (p->removed.load() || l_new != l) return false;
	      node* new_l = leaf_pool.new_obj(k, v);
	      if (k == l->key) *ptr = new_l; // update existing key (only if upsert)
	      else *ptr = ((l->is_sentinal || k > l->key) ?
			   internal_pool.new_obj(k, l, new_l) :
			   internal_pool.new_obj(l->key, new_l, l));
	      return true;});
	if (r) return (k != l->key);
      }});
  }

  bool remove(internal* root, K k) {
    return verlib::with_epoch([=] {
      leaf* prev_leaf = nullptr;
       while (true) {
	 auto [gp, gp_left, p, p_left, l] = find_location(root, k);
	 if (l->is_sentinal || !equal(k, l->key) ||
	     (prev_leaf != nullptr && prev_leaf != l))
	   return false;
	 prev_leaf = l;
	 if (gp->try_lock([=] {
	       return p->try_lock([=] {
		   auto ptr = gp_left ? &(gp->left) : &(gp->right);
		   if (gp->removed.load() || ptr->load() != p) return false;
		   node* ll = (p->left).load();
		   node* lr = (p->right).load();
		   if (p_left) std::swap(ll,lr);
		   if (lr != l) return false;
		   p->removed = true;
		   (*ptr) = ll; // shortcut
		   internal_pool.retire(p);
		   leaf_pool.retire(l);
		   return true; });}))
	   return true;
       }}); 
  }

  std::optional<V> find_(internal* root, K k) {
    auto ptr = &(root->left);
    node* l = ptr->read();
    while (!l->is_leaf) {
      internal* p = (internal*) l;
      auto ptr = less(k, p->key) ? &(p->left) : &(p->right);
      l = ptr->read();
    }
    ptr->validate();
    auto ll = (leaf*) l;
    if (!ll->is_sentinal && ll->key == k) return ll->value; 
    else return {};
  }

  std::optional<V> find(internal* root, K k) {
    return verlib::with_epoch([&] { return find_(root, k);});
  }

  internal* empty() {
      return internal_pool.new_obj(leaf_pool.new_obj());
  }

  internal* empty(size_t n) { return empty(); }
  
  void print(internal* p) {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
	     if (p->is_leaf) std::cout << p->key << ", ";
	     else {
	       internal* pp = (internal*) p;
	       prec((pp->left).load());
	       prec((pp->right).load());
	     }
	   };
    prec(p->left.load());
    std::cout << std::endl;
  }

  void retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) leaf_pool.retire((leaf*) p);
    else {
      internal* pp = (internal*) p;
      parlay::par_do([&] () { retire((pp->left).load()); },
		     [&] () { retire((pp->right).load()); });
      internal_pool.retire(pp);
    }
  }
  
  // return total height
  double total_height(internal* p) {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
	     if (p->is_leaf) return depth;
	     internal* pp = (internal*) p;
	     size_t d1, d2;
	     parlay::par_do([&] () { d1 = hrec((pp->left).load(), depth + 1);},
			    [&] () { d2 = hrec((pp->right).load(), depth + 1);});
	     return d1 + d2;
	   };
    return hrec(p->left.load(), 1);
  }

  long check(internal* p) {
    using rtup = std::tuple<K, K, long>;
    std::function<rtup(node*)> crec;
    crec = [&] (node* p) {
	     if (p->is_sentinal) return rtup(p->key, p->key, 0);
	     if (p->is_leaf) return rtup(p->key, p->key, 1);
	     internal* pp = (internal*) p;
	     K lmin,lmax,rmin,rmax;
	     long lsum, rsum;
	     parlay::par_do([&] () { std::tie(lmin,lmax,lsum) = crec((pp->left).load());},
			    [&] () { std::tie(rmin,rmax,rsum) = crec((pp->right).load());});
	     if ((lsum !=0 && !less(lmax, pp->key)) || less(rmin, pp->key))
	       std::cout << "out of order key: " << lmax << ", " << pp->key << ", " << rmin << std::endl;
	     if (lsum == 0) return rtup(pp->key, rmax, rsum);
	     else return rtup(lmin, rmax, lsum + rsum);
	   };
    auto [minv, maxv, cnt] = crec(p->left.load());
    //std::cout << "average height = " << ((double) total_height(p) / cnt) << std::endl;
    return cnt;
  }

  void clear() {
    internal_pool.clear();
    leaf_pool.clear();
  }

  void reserve(size_t n) {
    internal_pool.reserve(n);
    leaf_pool.reserve(n);
  }

  void shuffle(size_t n) {
    internal_pool.shuffle(n);
    leaf_pool.shuffle(n);
  }

  void stats() {
    internal_pool.stats();
    leaf_pool.stats();
  }
  
};
