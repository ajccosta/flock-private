#include <verlib/verlib.h>
#include <parlay/primitives.h>
#define Range_Search 1
#define RADIX 1

template <typename K>
struct int_string {
  static int length(const K& key) { return sizeof(K);}
  static int get_byte(const K& key, int pos) {
    return (key >> (8*(sizeof(K)-1-pos))) & 255;
  }
};

template <typename K,
	  typename V,
	  typename String = int_string<K>>
struct Set {

  enum node_type : char {Full, Indirect, Sparse, Leaf};

  constexpr static int max_indirect_size = 64;
  constexpr static int max_sparse_size = 16;
  static constexpr int max_leaf_size = 14;

  
  struct header : verlib::versioned {
    const node_type nt;
    char size;
    // every node has a byte position in the key
    // e.g. the root has byte_num = 0
    short int byte_num; 
    K key;
    header(node_type nt, char size) : nt(nt), size(size) {}
    header(const K& key, node_type nt, char size, int byte_num)
      : key(key), nt(nt), size(size), byte_num((short int) byte_num) {}
  };

  // generic node
  struct node : header {
    verlib::atomic_bool removed;
    verlib::lock lck;
    node(node_type nt, char size)
      : header(nt, size), removed(false) {}
    node(const K& key, node_type nt, char size, int byte_num)
      : header(key, nt, size, byte_num), removed(false) {}
  };
  
  using node_ptr = verlib::versioned_ptr<node>;

  // 256 entries, one for each value of a byte, null if empty
  struct full_node : node {
    node_ptr children[256];

    bool is_full() {return false;}

    node_ptr* get_child(const K& k) {
      auto b = String::get_byte(k, header::byte_num);
      return &children[b];}

    void init_child(int k, node* c) {
      auto b = String::get_byte(k, header::byte_num);
      children[b].init(c);
    }

    full_node() : node(Full, 0) {}
  };

  // Up to max_indirect_size entries, with array of 256 1-byte
  // pointers to the entries.  Adding a new child requires a copy.
  // Updating an existing child is done in place.
  struct indirect_node : node {
    char idx[256];
    node_ptr ptr[max_indirect_size];

    bool is_full() {return node::size == max_indirect_size;}
    
    node_ptr* get_child(const K& k) {
      int i = idx[String::get_byte(k, header::byte_num)];
      if (i == -1) return nullptr;
      else return &ptr[i];}

    void init_child(const K& k, node* c) {
      int i = node::size -1;
      idx[String::get_byte(k, header::byte_num)] = i;
      ptr[i].init(c);
    }

    // an empty indirect node
    indirect_node() : node(Indirect, 0) {};
  };

  // Up to max_sparse_size entries each consisting of a key and
  // pointer.  The keys are immutable, but the pointers can be
  // changed.  i.e. Adding a new child requires copying, but updating
  // a child can be done in place.
  struct alignas(64) sparse_node : node {
    unsigned char keys[max_sparse_size];
    node_ptr ptr[max_sparse_size];

    bool is_full() {return node::size == max_sparse_size;}

    node_ptr* get_child(const K& k) {
      __builtin_prefetch (((char*) ptr) + 64);
      int kb = String::get_byte(k, header::byte_num);
      for (int i=0; i < node::size; i++) 
	if (keys[i] == kb) return &ptr[i];
      return nullptr;
    }

    void init_child(const K& k, node* c) {
      int kb = String::get_byte(k, header::byte_num);
      keys[node::size-1] = kb;
      ptr[node::size-1].init(c);
    }

    // constructor for a new sparse node with two children
    sparse_node(int byte_num, node* v1, const K& k1, node* v2, const K& k2)
      : node(k1, Sparse, 2, byte_num) {
      if (k1 == k2) {
	std::cout << "new sparse: " << k1 << ", " << k2 << ", " << byte_num << std::endl;
	abort();
      }
      keys[0] = String::get_byte(k1, byte_num);
      ptr[0].init(v1);
      keys[1] = String::get_byte(k2, byte_num);
      ptr[1].init(v2);
    }

    sparse_node(int byte_num, node** start, node** end) :
      node((*start)->key, Sparse, end-start, byte_num) {
      for (int i=0; i < (end-start); i++) {
	keys[i] = String::get_byte((*(start+i))->key, byte_num);
	ptr[i] = *(start + i);
      }
    }
    
    // an empty sparse node
    sparse_node() : node(Sparse,0) {}
  };

  struct KV {K key; V value;};
  
  struct alignas(64) leaf : header {
    KV key_vals[max_leaf_size];
    bool equal(const K& k1, const K& k2) {
      if (String::length(k1) != String::length(k2)) return false;
      for (int i = header::byte_num; i < String::length(k1); i++)
	if (String::get_byte(k1,i) != String::get_byte(k2,i)) return false;
      return true;
    }
    std::optional<V> find(const K& k) {
      for (int i = 0; i < header::size; i++)
	//if (equal(key_vals[i].key, k)) return key_vals[i].value;
	if (key_vals[i].key == k) return key_vals[i].value;
      return {};}

    static int first_diff(int byte_pos, KV* start, KV* end) {
      if (end-start == 1) return String::length(start->key);
      int j = byte_pos;
      while (true) {
	int byteval = String::get_byte(start->key, j);
	for (KV* ptr = start + 1; ptr < end; ptr++) {
	  if (j >= String::length(ptr->key) ||
	      String::get_byte(ptr->key, j) != byteval) return j;
	}
	j++;
      }
    }

    // create singleton leaf
    leaf(K& key, V& value) : header(key, Leaf, 1, String::length(key)) {
      key_vals[0] = KV{key, value};
    }


    // create multi leaf
    leaf(int byte_pos, KV* start, KV* end)
      : header(start->key, Leaf, end-start, first_diff(byte_pos, start, end)) {
      for (int i=0; i < (end-start); i++) key_vals[i] = start[i];
    }

    // add to leaf
    leaf(int byte_pos, leaf* l, K& key, V& value) 
      : header(l->key, Leaf, l->size + 1, byte_pos) {
      for (int i=0; i < l->size; i++)
	key_vals[i] = l->key_vals[i];
      key_vals[l->size] = KV{key,value};
    }

    // remove from leaf
    leaf(leaf* l, K& key) 
      : header(l->key, Leaf, l->size - 1, 0) {
      int j = 0;
      for (int i=0; i < l->size; i++) {
	if (l->key_vals[i].key != key) 
	  key_vals[j++] = l->key_vals[i];
      }
      header::key = key_vals[0].key;
      header::byte_num = first_diff(l->byte_num, &key_vals[0], &key_vals[header::size]);
    }

  };

  verlib::memory_pool<full_node> full_pool;
  verlib::memory_pool<indirect_node> indirect_pool;
  verlib::memory_pool<sparse_node> sparse_pool;
  verlib::memory_pool<leaf> leaf_pool;

  // dispatch based on node type
  // A returned nullptr means no child matching the key
  inline node_ptr* get_child(node* x, const K& k) {
    switch (x->nt) {
    case Full : return ((full_node*) x)->get_child(k);
    case Indirect : return ((indirect_node*) x)->get_child(k);
    case Sparse : return ((sparse_node*) x)->get_child(k);
    }
    return nullptr;
  }

  // dispatch based on node type
  inline bool is_full(node* p) {
    switch (p->nt) {
    case Full : return ((full_node*) p)->is_full(); // never full
    case Indirect : return ((indirect_node*) p)->is_full();
    case Sparse : return ((sparse_node*) p)->is_full();
    }
    return false;
  }

  // Adds a new child to p with key k and value v
  // gp is p's parent (i.e. grandparent)
  // This involve copying p and updating gp to point to it.
  // This should never be called on a full node.
  // Returns false if it fails.
  bool add_child(node* gp, node* p, const K& k, const V& v) {
    return gp->lck.try_lock([=] {
        auto child_ptr = get_child(gp, p->key);
	bool rm = gp->removed.load();
	bool pt = child_ptr->load() != p;
	if (gp->removed.load() || child_ptr->load() != p)
	  return false;
	return p->lck.try_lock([=] {
            if (get_child(p,k) != nullptr) return false;
	    node* c = (node*) leaf_pool.new_obj(k, v);
	    if (p->nt == Indirect) {
	      indirect_node* i_n = (indirect_node*) p;
	      i_n->removed = true; 
	      if (is_full(p)) {
		// copy indirect to full
		*child_ptr = (node*) full_pool.new_init([=] (full_node* f_n) {
 		  f_n->key = i_n->key;
		  f_n->byte_num = i_n->byte_num;
		  for (int i=0; i < 256; i++) {
		    int j = i_n->idx[i];
		    if (j != -1) f_n->children[i].init(i_n->ptr[j].load());
		  }
		  // seemingly broken g++ (10.3) compiler makes the following fail
		  //   f_n->init_child(k, c);
		  // inlining by hand fixes it.
		  auto b = String::get_byte(k, f_n->byte_num);
		  f_n->children[b].init(c);});
	      } else {
		// copy indirect to indirect
		*child_ptr = (node*) indirect_pool.new_init([=] (indirect_node* i_c) {
  		  i_c->key = i_n->key;
		  i_c->byte_num = i_n->byte_num;
		  i_c->size = i_n->size + 1;
		  for (int i=0; i < 256; i++)
		    i_c->idx[i] = i_n->idx[i];
		  for (int i=0; i < i_n->size; i++) 
		    i_c->ptr[i].init(i_n->ptr[i].load());
		  i_c->init_child(k, c);});
		}
		indirect_pool.retire(i_n);
	      } else { // (p->nt == Sparse)
		sparse_node* s_n = (sparse_node*) p;
		s_n->removed = true;
		if (is_full(p)) {
		  // copy sparse to indirect
		  *child_ptr = (node*) indirect_pool.new_init([=] (indirect_node* i_n) {
  		    i_n->key = s_n->key;
		    i_n->byte_num = s_n->byte_num;
		    i_n->size = max_sparse_size + 1;
		    for (int i=0; i < 256; i++) i_n->idx[i] = -1;
		    for (int i=0; i < max_sparse_size; i++) {
		      i_n->idx[s_n->keys[i]] = i; 
		      i_n->ptr[i].init(s_n->ptr[i].load());
		    }
		    i_n->init_child(k, c);
	          });  
		} else {
		  // copy sparse to sparse
		  *child_ptr = (node*) sparse_pool.new_init([=] (sparse_node* s_c) {
  		    s_c->key = s_n->key;
		    s_c->byte_num = s_n->byte_num;
		    s_c->size = s_n->size + 1;
		    for (int i=0; i < s_n->size; i++) {
		      s_c->keys[i] = s_n->keys[i];
		      s_c->ptr[i].init(s_n->ptr[i].load());
		    }
		    s_c->init_child(k, c);});
		}
		sparse_pool.retire(s_n);
	    }
	    return true;}); // end try_lock(p->lck
	return true;}); // end try_lock(gp->lck
  }

  //*** will return one of 4 things: no child, empty child, leaf, subtree (cut edge)
  auto find_location(node* root, const K& k) {
    int byte_pos = 0;
    node* gp = nullptr;
    node* p = root;
    while (true) {
      node_ptr* cptr = get_child(p, k);
      if (cptr == nullptr) // has no child with given key
	return std::make_tuple(gp, p, cptr, (node*) nullptr, byte_pos);
      // could be read()
      node* c = cptr->load();
      if (c == nullptr) // has empty child with given key
	return std::make_tuple(gp, p, cptr, c, byte_pos);

      byte_pos++;
      while (byte_pos < c->byte_num &&
	     String::get_byte(k, byte_pos) == String::get_byte(c->key, byte_pos))
	byte_pos++;

      if (byte_pos != c->byte_num || c->nt == Leaf) 
	return std::make_tuple(gp, p, cptr, c, byte_pos);

      gp = p;
      p = c;
    }
  }

  bool insert_(node* root, const K& k, const V& v) {
    return flck::try_loop([&] () {return try_insert(root, k, v);});}

  bool insert(node* root, const K& k, const V& v) {
    return verlib::with_epoch([=] { return insert_(root, k, v);});}
  
  std::optional<bool> try_insert(node* root, const K& k, const V& v) {
    auto [gp, p, cptr, c, byte_pos] = find_location(root, k);
    //std::cout << byte_pos << ": " << std::hex << k << std::dec << std::endl;
    if (c != nullptr && c->nt == Leaf &&
	((leaf*) c)->find(k).has_value()) // already in the tree
      if (verlib::validate(p->lck, cptr, c) &&
	  verlib::validate(p->lck, &p->removed, false))
	return false;
      else return {};

    if (cptr != nullptr) {// child pointer exists, always true for full node
      if (p->lck.try_lock([=] {
			    //			    std::cout << "h" << std::endl;
 	  // exit and retry if state has changed
	  if (p->removed.load() || cptr->load() != c) return false;

	  // fill a null pointer with the new leaf
	  if (c == nullptr)
	    (*cptr) = (node*) leaf_pool.new_obj(k, v);
	  else if (c->nt == Leaf) {
	    //std::cout << "in leaf" << std::endl;
	    leaf* l = (leaf*) c;
	    if (l->size < max_leaf_size) {
	      *cptr = (node*) leaf_pool.new_obj(byte_pos, l, k, v);
	      leaf_pool.retire(l);
	    } else { // too large
	      int n = max_leaf_size + 1;
	      KV tmp[n];
	      for (int i=0; i < max_leaf_size; i++)
		tmp[i] = l->key_vals[i];
	      tmp[max_leaf_size] = KV{k,v};
	      auto gb = [&] (const KV& kv) {return String::get_byte(kv.key, byte_pos);};
	      std::sort(tmp, tmp + n, [&] (KV& a, KV& b) {return gb(a) < gb(b);});
	      int j = 0;
	      int start = 0;
	      int byteval = gb(tmp[0]);
	      node* children[n];
	      //std::cout << "mid: " << start << ", " << n << std::endl;
	      for (int i=1; i < n; i++) {
		if (gb(tmp[i]) != byteval) {
		  //std::cout << "first" << std::endl;
		  children[j++] = (node*) leaf_pool.new_obj(byte_pos+1, &tmp[start], &tmp[i]);
		  start = i;
		  byteval = gb(tmp[i]);
		}
	      }
	      //std::cout << "second: " << n << ", " << std::endl;
	      children[j++] = (node*) leaf_pool.new_obj(byte_pos+1, &tmp[start], &tmp[n]);
	      //std::cout << "mid2: " << start << ", " << n << ", " << j << std::endl;
	      *cptr = (node*) sparse_pool.new_obj(byte_pos, &children[0], &children[j]);
	      leaf_pool.retire(l);
	      //std::cout << "end" << std::endl;
	    }
	  } else { // not a leaf
	    node* new_l = (node*) leaf_pool.new_obj(k, v);
	    *cptr = (node*) sparse_pool.new_obj(byte_pos, c, c->key,
						new_l, k);
	  }
	  return true;})) return true;
    } else { // no child pointer, need to add
      bool x = add_child(gp, p, k, v);
      //std::cout << "a: " << check(root) << std::endl;
      if (x) return true;
    }
    return {};
  }

  // returns other child if node is sparse and has two children, one
  // of which is c, otherwise returns nullptr
  node* single_other_child(node* p, node* c) {
    if (p->nt != Sparse) return nullptr;
    sparse_node* ps = (sparse_node*) p;
    node* result = nullptr;
    for (int i=0; i < ps->size; i++) {
      node* oc = ps->ptr[i].load();
      if (oc != nullptr && oc != c)
	if (result != nullptr) return nullptr; // quit if second child
	else result = oc; // set first child
    }
    return result;
  }

  bool remove_(node* root, const K& k) {
    return flck::try_loop([&] () {return try_remove(root, k);});}

  bool remove(node* root, const K& k) {
    return verlib::with_epoch([=] { return remove_(root, k);});}

  // currently a "lazy" remove that only removes
  //   1) the leaf
  //   2) its parent if it is sparse with just two children
  std::optional<bool> try_remove(node* root, const K& k) {
    auto [gp, p, cptr, c, byte_pos] = find_location(root, k);
    // if not found return
    if (c == nullptr || !(c->nt == Leaf && ((leaf*) c)->find(k).has_value()))
      if (((cptr == nullptr) || verlib::validate(p->lck, cptr, c)) &&
	  verlib::validate(p->lck, &p->removed, false))
	return false;
      else return {};
    if (p->lck.try_lock([=] {
        if (p->removed.load() || cptr->load() != c) return false;
	leaf* l = (leaf*) c;
	if (l->size == 1) {
	  node* other_child = single_other_child(p, c);
	  if (other_child != nullptr && gp->nt != Sparse) {
	    // if parent will become singleton try to remove parent as well
	    return gp->lck.try_lock([=] {
	      auto child_ptr = get_child(gp, p->key);
	      if (gp->removed.load() || child_ptr->load() != p)
		return false;
	      *child_ptr = other_child;
	      p->removed = true;
	      sparse_pool.retire((sparse_node*) p);
	      leaf_pool.retire(l);
	      return true;});
	  } else { // just remove child
	    *cptr = nullptr; 
	    leaf_pool.retire(l);
	    return true;
	  }
	} else {
	  // remove key from leaf
	  *cptr = (node*) leaf_pool.new_obj(l, k);
	  leaf_pool.retire(l);
	  return true;
	}})) return true;
    return {};
  }

  std::optional<std::optional<V>> try_find(node* root, const K& k) {
    using ot = std::optional<std::optional<V>>;
    auto [gp, p, cptr, l, pos] = find_location(root, k);
    if (((cptr == nullptr) || verlib::validate(p->lck, cptr, l)) &&
	verlib::validate(p->lck, &p->removed, false))
      if (l == nullptr) return ot(std::optional<V>());
      else return ot(((leaf*) l)->find(k));
    else return ot();
  }

  std::optional<V> find_(node* root, const K& k) {
    auto [gp, p, cptr, l, pos] = find_location(root, k);
    auto ll = (leaf*) l;
    if (l == nullptr) return {};
    else return ((leaf*) l)->find(k);
  }

  std::optional<V> find(node* root, const K& k) {
    return verlib::with_epoch([&] {return find_(root, k);}); }

  std::optional<V> find_locked(node* root, const K& k) {
    return flck::try_loop([&] {return try_find(root, k);}); }

  template<typename AddF>
  void range_internal(node* a, AddF& add,
		      std::optional<K> start, std::optional<K> end, int pos) {
    if (a == nullptr) return;
    std::optional<K> empty;
    if (a->nt == Leaf) {
      if ((!start.has_value() || start.value() <= a->key)
	  && (!end.has_value() || end.value() >= a->key)) 
	add(a->key, ((leaf*) a)->key_vals[0].value);
      return;
    }
    for (int i = pos; i < a->byte_num; i++) {
      if (start == empty && end == empty) break;
      if (start.has_value()
	  && String::get_byte(start.value(), i) > String::get_byte(a->key, i)
	  || end.has_value()
	  && String::get_byte(end.value(), i) < String::get_byte(a->key, i))
	return;
      if (start.has_value() &&
	  String::get_byte(start.value(), i) < String::get_byte(a->key,i)) 
	start = empty;
      if (end.has_value() &&
	  String::get_byte(end.value(), i) > String::get_byte(a->key, i)) {
	end = empty;
      }
    }
    int sb = start.has_value() ? String::get_byte(start.value(), a->byte_num) : 0;
    int eb = end.has_value() ? String::get_byte(end.value(), a->byte_num) : 255;
    if (a->nt == Full) {
      for (int i = sb; i <= eb; i++) 
	range_internal(((full_node*) a)->children[i].read_snapshot(), add,
		       start, end, a->byte_num);
    } else if (a->nt == Indirect) {
      for (int i = sb; i <= eb; i++) {
	indirect_node* ai = (indirect_node*) a;
	int o = ai->idx[i];
	if (o != -1) {
	  range_internal(ai->ptr[o].read_snapshot(), add,
				    start, end, a->byte_num);
	}
      }
    } else { // Sparse
      sparse_node* as = (sparse_node*) a;
      for (int i = 0; i < as->size; i++) {
	int b = as->keys[i];
	if (b >= sb && b <= eb)
	  range_internal(as->ptr[i].read_snapshot(), add, start, end, a->byte_num);
      }
    }
  }		       

  template<typename AddF>
  void range_(node* root, AddF& add, const K& start, const K& end) {
    range_internal(root, add,
		   std::optional<K>(start), std::optional<K>(end), 0);
  }

  node* empty() {
    auto r = full_pool.new_obj();
    r->byte_num = 0;
    return (node*) r;
  }

  node* empty(size_t n) { return empty(); }
  
  void print(node* p) {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
	     if (p == nullptr) return;
	     switch (p->nt) {
	     case Leaf : {
	       auto l = (leaf*) p;
	       std::cout << std::hex << l->key << ":" << l->key_vals[0].key << std::dec << ", ";
	       return;
	     }
	     case Full : {
	       auto f_n = (full_node*) p;
	       for (int i=0; i < 256; i++) {
		 prec(f_n->children[i].load());
	       }
	       return;
	     }
	     case Indirect : {
	       auto i_n = (indirect_node*) p;
	       for (int i=0; i < 256; i++) {
		 int j = i_n->idx[i];
		 if (j != -1) prec(i_n->ptr[j].load());
	       }
	       return;
	     }
	     case Sparse : {
	       using pr = std::pair<int,node*>;
	       auto s_n = (sparse_node*) p;
	       std::vector<pr> v;
	       for (int i=0; i < s_n->size; i++)
		 v.push_back(std::make_pair(s_n->keys[i], s_n->ptr[i].load()));
	       std::sort(v.begin(), v.end()); 
	       for (auto x : v) prec(x.second);
	       return;
	     }
	     }
	   };
    prec(p);
    std::cout << std::endl;
  }

  void retire(node* p) {
    if (p == nullptr) return;
    if (p->nt == Leaf) leaf_pool.retire((leaf*) p);
    else if (p->nt == Sparse) {
      auto pp = (sparse_node*) p;
      parlay::parallel_for(0, pp->size, [&] (size_t i) {retire(pp->ptr[i].load());});
      sparse_pool.retire(pp);
    } else if (p->nt == Indirect) {
      auto pp = (indirect_node*) p;
      parlay::parallel_for(0, pp->size, [&] (size_t i) {retire(pp->ptr[i].load());});
      indirect_pool.retire(pp);
    } else {
      auto pp = (full_node*) p;
      parlay::parallel_for(0, 256, [&] (size_t i) {retire(pp->children[i].load());});
      full_pool.retire(pp);
    }
  }
  
  long check(node* p) {
    std::function<size_t(node*)> crec;
    crec = [&] (node* p) -> size_t {
	     if (p == nullptr) return 0;
	     switch (p->nt) {
	     case Leaf : return ((leaf*) p)->size;
	     case Full : {
	       auto f_n = (full_node*) p;
	       auto x = parlay::tabulate(256, [&] (size_t i) {
						return crec(f_n->children[i].load());});
	       return parlay::reduce(x);
	     }
	     case Indirect : {
	       auto i_n = (indirect_node*) p;
	       auto x = parlay::tabulate(256, [&] (size_t i) {
						int j = i_n->idx[i];
						return (j == -1) ? 0 : crec(i_n->ptr[j].load());});
	       return parlay::reduce(x);
	     }
	     case Sparse : {
	       auto s_n = (sparse_node*) p;
	       auto x = parlay::tabulate(s_n->size,
					 [&] (size_t i) {return crec(s_n->ptr[i].load());});
	       return parlay::reduce(x);
	     }
	     }
	     return 0;
	   };
    size_t cnt = crec(p);
    return cnt;
  }

  void clear() {
    full_pool.clear();
    indirect_pool.clear();
    sparse_pool.clear();
    leaf_pool.clear();
  }

  void reserve(size_t n) {}
  
  void shuffle(size_t n) {
    full_pool.shuffle(n/100);
    indirect_pool.shuffle(n/10);
    sparse_pool.shuffle(n/5);
    leaf_pool.shuffle(n);
  }

  void stats() {
    full_pool.stats();
    indirect_pool.stats();
    sparse_pool.stats();
    leaf_pool.stats();
  }
  
};
