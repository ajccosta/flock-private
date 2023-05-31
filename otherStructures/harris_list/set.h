
#ifndef LLCODE_ADAPTER_H
#define LLCODE_ADAPTER_H

#include "HarrisLinkedList.hpp"

#include <parlay/primitives.h>
#include <optional>

using namespace std;

template <typename K, typename V>
class ordered_map {
private:
  using adapter_t = HarrisLinkedList<K,V>;
  adapter_t* map;

public:
  static void reserve(size_t n) { adapter_t::node_pool.reserve(n); }

  ordered_map(size_t n) : map(new adapter_t()) {}

  std::optional<V> find(const K& key) {
    return map->find(key);
  } 

  std::optional<V> find_(const K& key) {
    return map->find_(key);
  } 

  bool insert(const K& key, const V& val) {
    return map->add(key, val);
  }

  bool remove(const K& key) {
    return map->remove(key);
  }

  void print() { }

  ~ordered_map() {
    delete map;
  }

  static void clear() {
    adapter_t::node_pool.clear();
  }

  size_t check() {
    return map->get_size();
  }

  static void stats() {
    adapter_t::node_pool.stats();
  }

  static void shuffle(size_t n) {
    adapter_t::node_pool.shuffle(n);
  }

};

#endif // LLCODE_ADAPTER_H
