
#include "include/parlay/primitives.h"

struct Zipfian {
public:
  static constexpr double kZipfianConst = 0.75; //99;
  static constexpr uint64_t kMaxNumItems = (UINT64_MAX >> 24);

  Zipfian(uint64_t num_items, double zipfian_const, double seed) :
  items_(num_items), theta_(zipfian_const), zeta_n_(num_items), seed(seed) {
    assert(items_ >= 2 && items_ < kMaxNumItems);
    zeta_2_ = Zeta(2, theta_);
    zeta_n_ = Zeta(num_items, theta_);
    alpha_ = 1.0 / (1.0 - theta_);
    eta_ = Eta();
  }

  uint64_t operator () (size_t i) {
    uint64_t r = parlay::hash64(seed+i);
    double u = ((double) r)/((double) std::numeric_limits<uint64_t>::max()); // uniform between 0.0 and 1.0
    double uz = u * zeta_n_;
    if (uz < 1.0) return 0;
    if (uz < 1.0 + std::pow(0.5, theta_)) return 1;
    return std::lround((items_-1) * std::pow(eta_ * u - eta_ + 1, alpha_));
  }

  double Eta() {
    return (1 - std::pow(2.0 / items_, 1 - theta_)) / (1 - zeta_2_ / zeta_n_);
  }

  static double Zeta(uint64_t cur_num, double theta) {
    double sum = 0;
    for(uint64_t i = 0; i < cur_num; i++) {
      sum += 1.0/ std::pow(i+1, theta);
    }
    return sum;
    // return parlay::reduce(parlay::delayed_tabulate(cur_num, [=] (size_t i) {
      // return 1.0/ std::pow(i+1, theta);}));
  }

  uint64_t items_;
  double theta_, zeta_n_, eta_, alpha_, zeta_2_, seed;
};

// template<typename T >
struct alignas(128) MyRandom {
  int N;
  double zipfian;
  uint64_t* nums;
  int index;
  int max;

  // MyRandom(int n, int max, double seed) : MyRandom(n, 0, max, seed) {}

  MyRandom(int n, double zipf, int max, uint64_t seed) : N(n), zipfian(zipf), nums(new uint64_t[N]), index(0), max(max) {
    // int P = std::thread::hardware_concurrency();
    // std::vector<std::thread> threads;
    // int work = (N+P-1)/P;
    std::mt19937_64 stdRand(seed);

    seed = parlay::hash64(seed) / 2;
    if(zipfian == 0) {
      for(int i = 0; i < N; i++) {
        // nums[i] = stdRand() % max;
        nums[i] = parlay::hash64(seed+i) % max;
        // std::cout << "seed: " << seed << ", i: " << i << ", hash: "<< parlay::hash64(seed+i) << std::endl;
      }
    } else {
      Zipfian z(max, zipfian, seed);
      // std::cout << "using zipfian parameter: " << zipfian << std::endl;
      // abort();
      for(int i = 0; i < N; i++) nums[i] = z(i);
    }
    // for(int p = 0; p < P; p++) {
    //   threads.push_back([&] {
    //     for(int i = p*work; i < (p+1)*work && i < N; i++) {
    //       if(zipfian == 0) // uniform distribution
    //         nums[i] = parlay::hash64(i) % max;
    //       else
    //         nums[i] = z(i);
    //     }
    //   });
    // }
    // for(auto th : threads) th.join();
  }

  uint64_t next() {
    // std::cout << "index " << index << ", nums[index] " << nums[index] << std::endl;
    uint64_t ret = nums[index++];
    if(index >= N) index = 0;
    return ret;
  }
};

