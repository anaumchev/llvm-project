#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

static const std::array<uint64_t, 1> ArraySizesQuick{177777777};
static const std::array<uint64_t, 14> ArraySizes{
    1,     100,     1024,    2048,     4096,     8192,      10000,
    81920, 1000000, 4194304, 23445657, 41943040, 100000000, 177777777};

struct Config {
  bool QuickRun = true;
  std::vector<uint64_t> ArraySizes;
};

enum class RedOp { Sum, Max, Min, Mult };

template <typename T, RedOp Op> constexpr T redIdentity() {
  if constexpr (Op == RedOp::Sum)
    return T(0);
  else if constexpr (Op == RedOp::Max)
    return std::numeric_limits<T>::lowest();
  else if constexpr (Op == RedOp::Min)
    return std::numeric_limits<T>::max();
  else if constexpr (Op == RedOp::Mult)
    return T(1);
  else
    static_assert(!std::is_same_v<T, T>, "Unsupported red op");
}

template <typename T, RedOp Op> constexpr T redCombine(T A, T B) {
  if constexpr (Op == RedOp::Sum)
    return A + B;
  else if constexpr (Op == RedOp::Max)
    return std::max(A, B);
  else if constexpr (Op == RedOp::Min)
    return std::min(A, B);
  else if constexpr (Op == RedOp::Mult)
    return A * B;
  else
    static_assert(!std::is_same_v<T, T>, "Unsupported red op");
}

template <typename T, RedOp Op> static T goldRed(const T *In, uint64_t N) {
  T A = redIdentity<T, Op>();
  for (uint64_t I = 0; I < N; I++)
    A = redCombine<T, Op>(A, In[I]);
  return A;
}

template <typename T> static T goldRedDot(const T *A, const T *B, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();
  for (uint64_t I = 0; I < N; I++)
    S += A[I] * B[I];
  return S;
}
