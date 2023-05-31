#include <map>

template <typename K,
	  typename V,
	  typename Compare = std::less<K>>
struct ordered_map {

  using Table = std::map<K, V, Compare>;
  Table table;

  std::optional<V> find(const K& k) {
    auto r = table.find(k);
    if (r != table.end()) return (*r).second;
    else return std::optional<V>();
  }

  std::optional<V> find_(const K& k) {
    return find(k);
  }

  bool insert(const K& k, const V& v) {
    return table.insert(std::make_pair(k, v)).second;    
  }

  bool remove(const K& k) {
    return table.erase(k) == 1;
  }

  ordered_map(size_t n) {}

  void print() {}
  long check() {return table.size();}
  static void clear() {}
  static void stats() {}
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
};
