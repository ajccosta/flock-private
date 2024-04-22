// MIT license (https://opensource.org/license/mit/)
// Initial Author: Guy Blelloch
// Developed as part of the flock library
// 

#ifndef PARLAYHASH_H_
#define PARLAYHASH_H_

#include <optional>
#include <verlib/verlib.h>
#include <parlay/primitives.h>

template <typename K_,
          typename V_,
          class Hash = std::hash<K_>,
          class KeyEqual = std::equal_to<K_>>
struct unordered_map {
  using K = K_;
  using V = V_;
  using KV = std::pair<K,V>;
  struct link : verlib::versioned {
    KV element;
    verlib::versioned_ptr<link> next;
    link(KV element, link* next) : element(element), next(next) {}
  };

  struct bucket
#ifndef UseCAS
    : verlib::lock
#endif
  {
    link* load() {return ptr.load();}
    verlib::versioned_ptr<link> ptr;
    bucket() : ptr(nullptr) {}
  };

  struct Table {
    parlay::sequence<bucket> table;
    size_t size;
    bucket* get_bucket(const K& k) {
      size_t idx = Hash{}(k) & (size-1u);
      return &table[idx];
    }
    Table(size_t n) {
      int bits = 1 + parlay::log2_up(n);
      size = 1ul << bits;
      table = parlay::sequence<bucket>(size);
    }
  };

  Table hash_table;
  static verlib::memory_pool<link> link_pool;

  unordered_map(size_t n) : hash_table(Table(n)) {}
    
  ~unordered_map() {
    auto& table = hash_table.table;
    parlay::parallel_for (0, table.size(), [&] (size_t i) {
      retire_all_list(table[i].load());});
  }

  static std::optional<V> find_in_list(link* nxt, const K& k) {
    while (nxt != nullptr && !KeyEqual{}(nxt->element.first, k))
      nxt = nxt->next.load();
    if (nxt == nullptr) return {};
    else return nxt->element.second;
  }

  static std::pair<int, link*> remove_from_list(link* nxt, const K& k) {
    if (nxt == nullptr) return std::pair(0, nullptr);
    else if (KeyEqual{}(nxt->element.first, k)) return std::pair(1, nxt->next.load()); 
    else {
      auto [len, ptr] = remove_from_list(nxt->next.load(), k);
      if (len == 0) return std::pair(len, ptr);
      return std::pair(len + 1, link_pool.New(nxt->element, ptr));
    }
  }

  static std::pair<int, link*> update_list(link* nxt, const K& k, const V& v) {
    if (nxt == nullptr)
      return std::pair(1, link_pool.New(std::pair(k,v), nullptr));
    else if (KeyEqual{}(nxt->element.first, k)) 
      return std::pair(1, link_pool.New(std::pair(k,v), nxt->next.load()));
    else {
      auto [len, ptr] = update_list(nxt->next.load(), k, v);
      if (len == 0) return std::pair(len, ptr);
      return std::pair(len + 1, link_pool.New(nxt->element, ptr));
    }
  }
    
  static void retire_list(link* nxt, int n) {
    while (n > 0) {
      n--;
      link* tmp = nxt->next.load();
      link_pool.Retire(nxt);
      nxt = tmp;
    }
  }

  static void retire_all_list(link* nxt) {
    while (nxt != nullptr) {
      link* tmp = nxt->next.load();
      link_pool.Retire(nxt);
      nxt = tmp;
    }
  }

  static int list_length(link* nxt) {
    int len = 0;
    while (nxt != nullptr) {
      len++;
      nxt = nxt->next.load();
    }
    return len;
  }
    
  std::optional<V> find(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    __builtin_prefetch (s);
    return epoch::with_epoch([&] {return find_in_list(s->load(), k);});
  }

  std::optional<V> find_(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    return find_in_list(s->load(), k);
  }

  std::optional<V> find_locked(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    return find_in_list(s->read_lock([=] {return s->load();}), k);
  }

  static bool weak_cas(bucket* s, link* old_v, link* new_v) {
#ifdef UseCAS
    return (s->load() == old_v &&
            s->ptr.cas(old_v, new_v)) ; 
#else  // use try_lock
    return (s->try_lock([=] {
      if (!verlib::validate([&] {return s->ptr.load() == old_v;}))
        return false;
      (s->ptr).store(new_v);
      return true;}));
#endif
  }

  static std::optional<bool> try_insert_at(bucket* s, const K& k, const V& v) {
    link* head = s->load();
    if (find_in_list(head, k).has_value()) {
#ifndef UseCAS
      if (s->read_lock([&] {return verlib::validate([&] {return s->load() == head;});}))
        return false;
      else return {};
#else
      return false;
#endif
    }
    link* new_head = link_pool.New(std::pair(k,v), head);
    if (weak_cas(s, head, new_head)) return true;
    link_pool.Delete(new_head);
    return {};
  }

  bool insert(const K& k, const V& v) {
    bucket* s = hash_table.get_bucket(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] {
      return flck::try_loop([&] {return try_insert_at(s, k, v);});});
  }

  bool insert_(const K& k, const V& v) {
    bucket* s = hash_table.get_bucket(k);
    return flck::try_loop([&] {return try_insert_at(s, k, v);});
  }

  static std::optional<bool> try_update_at(bucket* s, const K& k, const V& v) {
    link* head = s->load();
    auto [cnt, new_head] = update_list(head, k, v);
    if (weak_cas(s, head, new_head)) {
      retire_list(head, cnt - 1);
      return true;
    }
    retire_list(new_head, cnt);
    return {};
  }

  bool upsert(const K& k, const V& v) {
    bucket* s = hash_table.get_bucket(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] {
      return flck::try_loop([&] {return try_update_at(s, k, v);});});
  }

  bool upsert_(const K& k, const V& v) {
    bucket* s = hash_table.get_bucket(k);
    return flck::try_loop([&] {return try_update_at(s, k, v);});
  }

    
  static std::optional<bool> try_remove_at(bucket* s, const K& k) {
    link* head = s->load();
    auto [cnt, new_head] = remove_from_list(head, k);
    if (cnt == 0) {
#ifndef UseCAS
      if (s->read_lock([&] {return verlib::validate([&] {return s->load() == head;});}))
        return false;
      else return {};
#else
      return false;
#endif
    }
    if (weak_cas(s, head, new_head)) {
      retire_list(head, cnt);
      return true;
    }
    retire_list(new_head, cnt - 1);
    return {};
  }

  bool remove(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] {
      return flck::try_loop([&] {return try_remove_at(s, k);});});
  }

  bool remove_(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    return flck::try_loop([&] {return try_remove_at(s, k);});
  }

  long size() {
    return parlay::reduce(parlay::map(hash_table.table, [] (bucket& x) {return (long) list_length(x.load());}));
  }

  void print() {}
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
  static void stats() {}
  long check() { return size();}
  static void clear() {}

};

template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::link> unordered_map<K,V,H,E>::link_pool;
#endif //PARLAYHASH_H_
