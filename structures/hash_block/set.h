#define HASH 1 // indicates that Set needs a hash function

#include "unordered_map.h"

// glue used for testing harness
template <typename K,
	  typename V,
	  class Hash = std::hash<K>,
	  class KeyEqual = std::equal_to<K>>
struct Set {
  using mp = unordered_map<K,V,Hash,KeyEqual>;
  static mp empty(size_t n) { return mp(n);}
  static bool insert_(mp& map, const K& key, const V& value) {
    return map.insert_(key,value);}
  static bool insert(mp& map, const K& key, const V& value) {
    return map.insert(key,value);}
  static bool upsert(mp& map, const K& key, const V& value) {
    return map.upsert(key,value);}
  static bool remove(mp& map, const K& key) {
    return map.remove(key);}
  static bool remove_(mp& map, const K& key) {
    return map.remove_(key);}
  static std::optional<V> find(mp& map, const K& key) {
    return map.find(key);}
  static std::optional<V> find_(mp& map, const K& key) {
    return map.find_(key);}
  static std::optional<V> find_locked(mp& map, const K& key) {
    return map.find_locked(key);}
  static size_t check(mp& map) { return map.check(); }
  static void print(mp& map) { return map.print(); }
  static void retire(mp& map) {}
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
  static void clear() { mp::clear(); }
  static void stats() { mp::stats(); }
};
