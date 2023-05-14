#define Range_Search 1
#define RADIX 1
#include "ordered_map.h"

// glue used for testing harness
template <typename K,
          typename V,
	  typename String = int_string<K>>
struct Set {
  using mp = ordered_map<K,V,String>;
  static mp empty(size_t n) { return mp(n);}
  static bool insert(mp& map, const K& key, const V& value) {
    return map.insert(key,value);}
  //static bool upsert(mp& map, const K& key, const V& value) {
  //  return map.upsert(key,value);}
  static bool remove(mp& map, const K& key) {
    return map.remove(key);}
  static std::optional<V> find(mp& map, const K& key) {
    return map.find(key);}
  static std::optional<V> find_(mp& map, const K& key) {
    return map.find_(key);}
  static std::optional<V> find_locked(mp& map, const K& key) {
    return map.find_locked(key);}
  template<typename AddF>
  static void range_(mp& map, AddF& add, const K& start, const K& end) {
    return map.range_(add, start, end); }
  static size_t check(mp& map) { return map.check(); }
  static void print(mp& map) { return map.print(); }
  static void retire(mp& map) {}
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
  static void clear() { mp::clear(); }
  static void stats() { mp::stats(); }
};
