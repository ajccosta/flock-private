#define HASH 1

#include <mutex>
#include <unordered_map>
#include <atomic>

class SpinLock {
private:
  std::atomic_flag lck = ATOMIC_FLAG_INIT;
public:
  void lock() {
    while(lck.test_and_set(std::memory_order_acquire))
      for (volatile int i=0; i < 1000; i++); }
  void unlock() { lck.clear(std::memory_order_release); }
};


using LockType = std::mutex;
//using LockType = SpinLock;

template <typename K,
	  typename V,
	  class Hash = std::hash<K>,
	  class KeyEqual = std::equal_to<K>>
struct unordered_map {

  using umap = std::unordered_map<K, V, Hash, KeyEqual>;
  struct entry {
    LockType mutex;
    umap sub_table;
  };

  unsigned int hash_to_shard(const K& k) {
    return (Hash{}(k) * UINT64_C(0xbf58476d1ce4e5b9)) & (num_buckets-1);
  }
  
  std::vector<entry> table;
  long num_buckets;

  std::optional<V> find(const K& k) {
    size_t idx = hash_to_shard(k);
    const std::lock_guard<LockType> lock(table[idx].mutex);
    auto r = table[idx].sub_table.find(k);
    if (r != table[idx].sub_table.end()) return (*r).second;
    else return std::optional<V>();
  }

  std::optional<V> find_(const K& k) {
    return find(k);
  }

  bool insert(const K& k, const V& v) {
    size_t idx = hash_to_shard(k);
    const std::lock_guard<LockType> lock(table[idx].mutex);
    return table[idx].sub_table.insert(std::make_pair(k, v)).second;    
  }

  bool remove(const K& k) {
    size_t idx = hash_to_shard(k);
    const std::lock_guard<LockType> lock(table[idx].mutex);
    return table[idx].sub_table.erase(k) == 1;
  }

  unordered_map(size_t n) {
    int n_bits = std::round(std::log2(n));
    int bits = std::min(15, n_bits - 2);
    num_buckets = 1l << bits; // must be a power of 2

    table = std::vector<entry>(num_buckets);
    for (int i=0; i < num_buckets; i++)
      table[i].sub_table = umap(n/num_buckets);
  }

  void print() {}
  long check() {
    long n = 0;
    for (int i = 0; i < num_buckets; i++)
      n += table[i].sub_table.size();
    return n;}
  static void clear() {}
  static void stats() {}
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
};
