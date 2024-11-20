#ifndef PARLAY_INTERNAL_RANDOM_H_
#define PARLAY_INTERNAL_RANDOM_H_

namespace parlay {

// A deterministic random bit generator satisfying uniform_random_bit_generator
// Can therefore be used as the engine for C++'s <random> number generators.
// For example, to generate uniform random integers, write
//
//    parlay::random_generator gen;
//    std::uniform_int_distribution<vertex> dis(0, n-1);
//
// then generate random numbers by invoking dis(gen). Note that each generator
// should only be used by one thread at a time (they are not thread safe).
//
// To generate random numbers in parallel, the generator supports an operator[],
// which creates another generator seeded from the current state and a given seed.
// For example, to generate random numbers in parallel, you could write
//
//    parlay::random_generator gen;
//    std::uniform_int_distribution<vertex> dis(0, n-1);
//    auto result = parlay::tabulate(n, [&](size_t i) {
//      auto r = gen[i];
//      return dis(r);
//    });
//
// The quality of randomness should be good enough for simple randomized algorithms,
// but should not be relied upon for anything that requires high-quality randomness.
//
struct random_generator {
 public:
  using result_type = size_t;
  explicit random_generator(size_t seed) : state(seed) { }
  random_generator() : state(0) { }
  void seed(result_type value = 0) { state = value; }
  result_type operator()() { return state = hash64(state); }
  static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
  static constexpr result_type min() { return std::numeric_limits<result_type>::lowest(); }
  random_generator operator[](size_t i) const {
    return random_generator(static_cast<size_t>(hash64((i+1)*0x7fffffff + state))); }
 private:
  result_type state = 0;
};

struct random {
 public:
  random(size_t seed) : state(seed){}
  random() : state(0){}
  random fork(uint64_t i) const { return random(static_cast<size_t>(hash64(hash64(i + state)))); }
  random next() const { return fork(0); }
  size_t ith_rand(uint64_t i) const { return static_cast<size_t>(hash64(i + state)); }
  size_t operator[](size_t i) const { return ith_rand(i); }
  size_t rand() { return ith_rand(0); }
  size_t max() { return std::numeric_limits<size_t>::max(); }
 private:
  size_t state = 0;
};

} //namespace parlay

#endif
