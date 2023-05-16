// A concurrent unordered_map using a hash table
// Supports: insert, remove, find, print
// Each bucket points to a structure (Node) containing an array of entries
// Nodes come in varying sizes.
// On update the node is copied
// Works either with CAS or try_locks (based on UseCAS flag)

#include <parlay/primitives.h>
#include <verlib/verlib.h>

template <typename K,
	  typename V,
	  class Hash = std::hash<K>,
	  class KeyEqual = std::equal_to<K>>
struct unordered_map {

  // what each slot in table points to
  template <int Size>
  struct Node : verlib::versioned {
    using node = Node<0>;
    struct KV {K key; V value;};
    int cnt;
    KV entries[Size];
    int find(const K& k) {
      for (int i=0; i < cnt; i++)
	if (entries[i].key == k) return i;
      return -1;
    }

    // Node(K k, V v) : cnt(1) { // could this overload with the other constructor if K is a pointer?
    //   entries[0] = KV{k,v};
    // }
    
    // insert into old
    Node(node* old, const K& k, const V& v) {
      if (old == nullptr) cnt = 1;
      else {
	cnt = old->cnt + 1;
	for (int i=0; i < old->cnt; i++)
	  entries[i] = ((Node<Size>*) old)->entries[i];
      }
      entries[cnt-1] = KV{k,v};
    }

    // update old
    Node(node* old, const K& k, const V& v, bool x) : cnt(old->cnt) {
      assert(old != nullptr);
      int i = 0;
      while (!KeyEqual{}(k, old->entries[i].key)) {
	assert(i < old->cnt);
      	entries[i] = ((Node<Size>*) old)->entries[i];
	i++;
      }
      entries[i].key = ((Node<Size>*) old)->entries[i].key;
      entries[i].value = v;
      i++;
      while (i < cnt) {
      	entries[i] = ((Node<Size>*) old)->entries[i];
	i++;
      }
    }

    // delete from old
    Node(node* old, const K& k) : cnt(old->cnt - 1) {
      int i = 0;
      while (!KeyEqual{}(k, old->entries[i].key)) {
	assert(i < old->cnt);
      	entries[i] = ((Node<Size>*) old)->entries[i];
	i++;
      }
      while (i < cnt) {
      	entries[i] = ((Node<Size>*) old)->entries[i+1];
	i++;
      }
    }
  };
  using node = Node<0>;
  
struct slot {
#ifndef UseCAS
  verlib::lock lck;
#endif
    verlib::versioned_ptr<node> ptr;
    slot() : ptr(nullptr) {}
  };

  struct Table {
    parlay::sequence<slot> table;
    slot* get_slot(const K& k) {
      size_t idx = Hash{}(k)  & (table.size()-1u);
      return &table[idx];
    }
    Table(size_t n) {
      size_t size = (1ul << std::max(parlay::log2_up(n), 14ul));
      table = parlay::sequence<slot>(2*size);
    }
  };

  Table hash_table;
  unordered_map(size_t n) : hash_table(Table(n)) {}

  using Node1 = Node<1>;
  using Node3 = Node<3>;
  using Node7 = Node<7>;
  using Node31 = Node<31>;
  static verlib::memory_pool<Node1> node_pool_1;
  static verlib::memory_pool<Node3> node_pool_3;
  static verlib::memory_pool<Node7> node_pool_7;
  static verlib::memory_pool<Node31> node_pool_31;

  static node* insert_to_node(node* old, const K& k, const V& v) {
    if (old == nullptr) return (node*) node_pool_1.new_obj(old, k, v);
    if (old->cnt < 3) return (node*) node_pool_3.new_obj(old, k, v);
    else if (old->cnt < 7) return (node*) node_pool_7.new_obj(old, k, v);
    else if (old->cnt < 31) return (node*) node_pool_31.new_obj(old, k, v);
    else abort();
  }

  static node* update_node(node* old, const K& k, const V& v) {
    if (old == nullptr) return (node*) node_pool_1.new_obj(old, k, v, true);
    if (old->cnt < 3) return (node*) node_pool_3.new_obj(old, k, v, true);
    else if (old->cnt < 7) return (node*) node_pool_7.new_obj(old, k, v, true);
    else if (old->cnt < 31) return (node*) node_pool_31.new_obj(old, k, v, true);
    else abort();
  }

  static node* remove_from_node(node* old, const K& k) {
    if (old->cnt == 1) return (node*) nullptr;
    if (old->cnt == 2) return (node*) node_pool_1.new_obj(old, k);
    else if (old->cnt <= 4) return (node*) node_pool_3.new_obj(old, k);
    else if (old->cnt <= 8) return (node*) node_pool_7.new_obj(old, k);
    else return (node*) node_pool_31.new_obj(old, k);
  }

  static void retire_node(node* old) {
    if (old == nullptr);
    else if (old->cnt == 1) node_pool_1.retire((Node1*) old);
    else if (old->cnt <= 3) node_pool_3.retire((Node3*) old);
    else if (old->cnt <= 7) node_pool_7.retire((Node7*) old);
    else if (old->cnt <= 31) node_pool_31.retire((Node31*) old);
    else abort();
  }

  static void destruct_node(node* old) {
    if (old == nullptr);
    else if (old->cnt == 1) node_pool_1.destruct((Node1*) old);
    else if (old->cnt <= 3) node_pool_3.destruct((Node3*) old);
    else if (old->cnt <= 7) node_pool_7.destruct((Node7*) old);
    else if (old->cnt <= 31) node_pool_31.destruct((Node31*) old);
    else abort();
  }

  //__attribute__((always_inline))
  static std::optional<V> find_at(node* x, const K& k) {
    if (KeyEqual{}(x->entries[0].key, k))
      return x->entries[0].value;
    int i = x->find(k);
    if (i == -1) return {};
    else return x->entries[i].value;
  }

  std::optional<V> find_(const K& k) {
    slot* s = hash_table.get_slot(k);
    node* x = s->ptr.load();
    if (x == nullptr) return {};
    return find_at(x, k);
  }

  std::optional<V> find(const K& k) {
    slot* s = hash_table.get_slot(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] {
      node* x = s->ptr.load();
      if (x == nullptr) return std::optional<V>();
      return find_at(x, k);});
  }

  std::optional<V> find_locked(const K& k) {
    return flck::try_loop([&] {return try_find(k);});
  }

  std::optional<std::optional<V>> try_find(const K& k) {
    using ot = std::optional<std::optional<V>>;
    slot* s = hash_table.get_slot(k);
    node* x = s->ptr.load();
    if (x == nullptr) return ot(std::optional<V>());
    if (verlib::validate(s->lck, &s->ptr, x))
      return ot(find_at(x, k));
    return ot();
  }

  static std::optional<bool> try_insert_at(slot* s, const K& k, const V& v, bool upsert) {
    node* x = s->ptr.load();
    node* new_node;
    bool found = x != nullptr && x->find(k) != -1;
    if (found)
      if (upsert)
	new_node = update_node(x, k, v);
      else if (verlib::validate(s->lck, &(s->ptr), x))
	return false;
      else return {};
    else new_node = insert_to_node(x, k, v);
#ifdef UseCAS
    if(s->ptr.cas(x, new_node)) {
      retire_node(x);
      return true;
    } 
#else  // use try_lock
    if (s->lck.try_lock([=] {
        if (s->ptr.load() != x) return false;
	s->ptr = new_node;
	retire_node(x);
	return true;}))
      return !found;
#endif
    destruct_node(new_node); 
    return {}; // failed
  }
    
  bool insert(const K& k, const V& v) {
    slot* s = hash_table.get_slot(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] {
      return flck::try_loop([&] {return try_insert_at(s, k, v, false);});});
  }

  bool upsert(const K& k, const V& v) {
    slot* s = hash_table.get_slot(k); 
    return verlib::with_epoch([&] {
      return flck::try_loop([&] {return try_insert_at(s, k, v, true);});});
  }

   bool insert_(const K& k, const V& v) {
     slot* s = hash_table.get_slot(k); 
     return flck::try_loop([&] {return try_insert_at(s, k, v, false);});
   }
  
  static std::optional<bool> try_remove_at(slot* s, const K& k) {
      node* x = s->ptr.load();
      if (x == nullptr || x->find(k) == -1)
	if (verlib::validate(s->lck, &(s->ptr), x))
	  return false;
	else return {};
      node* new_node = remove_from_node(x, k);
#ifdef UseCAS
      if(s->ptr.cas(x, new_node)) {
        retire_node(x);
        return true;
      } 
#else
      if (s->lck.try_lock([=] {
	    if (s->ptr.load() != x) return false;
	    s->ptr = new_node;
	    retire_node(x);
	    return true;}))
	return true;
#endif
      destruct_node(new_node);
      return {};
  }

  bool remove(const K& k) {
    slot* s = hash_table.get_slot(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] {
      return flck::try_loop([&] {return try_remove_at(s, k);});});
  }

   bool remove_(const K& k) {
     slot* s = hash_table.get_slot(k); 
     return flck::try_loop([&] {return try_remove_at(s, k);});
   }

  void print() {
    auto& table = hash_table.table;
    for (size_t i=0; i < table.size(); i++) {
      node* x = table[i].ptr.load();
      if (x != nullptr)
	for (int i = 0; i < x->cnt; i++)
	  std::cout << x->entries[i].key << ", ";
    }
    std::cout << std::endl;
  }

  ~unordered_map() {
    auto& table = hash_table.table;
    parlay::parallel_for (0, table.size(), [&] (size_t i) {
      retire_node(table[i].ptr.load());});
  }
  
  long check() {
    auto& table = hash_table.table;
    auto s = parlay::tabulate(table.size(), [&] (size_t i) {
	      node* x = table[i].ptr.load();
	      if (x == nullptr) return 0;
	      else return x->cnt;});
    return parlay::reduce(s);
  }

  static void clear() {
    node_pool_1.clear();
    node_pool_3.clear();
    node_pool_7.clear();
    node_pool_31.clear();
  }
  static void stats() {
    node_pool_1.stats();
    node_pool_3.stats();
    node_pool_7.stats();
    node_pool_31.stats();
  }
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
};

template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node1> unordered_map<K,V,H,E>::node_pool_1;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node3> unordered_map<K,V,H,E>::node_pool_3;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node7> unordered_map<K,V,H,E>::node_pool_7;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node31> unordered_map<K,V,H,E>::node_pool_31;
