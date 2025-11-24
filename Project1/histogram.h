#pragma once
#ifndef CEPH_HISTOGRAM_H
#define CEPH_HISTOGRAM_H
#include <iostream>
#include <list>
#include <vector>



using namespace std;

// count bits (set + any 0's that follow)
#include <type_traits>
#include <limits>

template<class T>
inline typename std::enable_if<std::is_integral<T>::value, unsigned>::type portable_clz(T v) {
    if (v == 0) return sizeof(T) * 8;

    unsigned numBits = 0;
    while (!(v & (static_cast<T>(1) << (sizeof(T) * 8 - 1)))) {
        v <<= 1;
        ++numBits;
    }
    return numBits;
}

// 使用portable_clz代替__builtin_clzl和其他GCC特有函数
template<class T>
inline typename std::enable_if<(std::is_integral<T>::value && sizeof(T) <= sizeof(unsigned)), unsigned>::type cbits(T v) {
    if (v == 0) return 0;
    return portable_clz(v);
}

/**
 * power of 2 histogram
 * 数组h存放数据的bits位数的统计
 * 数据：0，1，2，3，4，5，6，7，8，9，10，7
 * bits：0，1，2，2，3，3，3，3，4，4，4，3
 * 数组h：1，1，2，5，4
 */
struct pow2_hist_t { //
  /**
   * histogram
   *
   * bin size is 2^index
   * value is count of elements that are <= the current bin but > the previous bin.
   */
  std::vector<int32_t> h;

private:
  /// expand to at least another's size
  void _expand_to(unsigned s) {
    if (s > h.size())
      h.resize(s, 0);
  }
  /// drop useless trailing 0's
  void _contract() {
    unsigned p = h.size();
    while (p > 0 && h[p-1] == 0)
      --p;
    h.resize(p);
  }

public:
  void clear() {
    h.clear();
  }
  bool empty() const {
    return h.empty();
  }
  void set_bin(int bin, int32_t count) {
    _expand_to(bin + 1);
    h[bin] = count;
    _contract();
  }

  void add(int32_t v) {
    int bin = cbits(v);
    _expand_to(bin + 1);
    h[bin]++;
    _contract();
  }

  bool operator==(const pow2_hist_t &r) const {
    return h == r.h;
  }

  /// get a value's position in the histogram.
  ///
  /// positions are represented as values in the range [0..1000000]
  /// (millionths on the unit interval).
  ///
  /// @param v [in] value (non-negative)
  /// @param lower [out] pointer to lower-bound (0..1000000)
  /// @param upper [out] pointer to the upper bound (0..1000000)
  int get_position_micro(int32_t v, uint64_t *lower, uint64_t *upper) {
    if (v < 0)
      return -1;
    unsigned bin = cbits(v);
    uint64_t lower_sum = 0, upper_sum = 0, total = 0;
    for (unsigned i=0; i<h.size(); ++i) {
      if (i <= bin)
	      upper_sum += h[i];
      if (i < bin)
	      lower_sum += h[i];
      total += h[i];
      //cout<<h[i]<<endl;
    }
    //cout<<endl;
    if (total > 0) {
      *lower = lower_sum * 1000000 / total;
      *upper = upper_sum * 1000000 / total;
    }
    return 0;
  }

  void add(const pow2_hist_t& o) {
    _expand_to(o.h.size());
    for (unsigned p = 0; p < o.h.size(); ++p)
      h[p] += o.h[p];
    _contract();
  }
  void sub(const pow2_hist_t& o) {
    _expand_to(o.h.size());
    for (unsigned p = 0; p < o.h.size(); ++p)
      h[p] -= o.h[p];
    _contract();
  }

  int32_t upper_bound() const {
    return 1 << h.size();
  }

  /// decay histogram by N bits (default 1, for a halflife)
  void decay(int bits = 1);

  static void generate_test_instances(std::list<pow2_hist_t*>& o);
};

#endif /* CEPH_HISTOGRAM_H */
