#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

#include "omp.h"

#ifndef XTEAM_NUM_THREADS
#define XTEAM_NUM_THREADS 512
#endif
#ifndef XTEAM_NUM_TEAMS
#define XTEAM_NUM_TEAMS 208 // gfx90a number of CUs: 104
#endif
// If true, let codegen for reduction/scan determine num_teams and num_threads.
#ifndef CODEGEN_AUTODETECTION
#define CODEGEN_AUTODETECTION 0
#endif
#if CODEGEN_AUTODETECTION
#define TEAMS
#define THREADS
#else // CODEGEN_AUTODETECTION
#define TEAMS num_teams(XTEAM_NUM_TEAMS)
#define THREADS num_threads(XTEAM_NUM_THREADS)
#endif // CODEGEN_AUTODETECTION
#define TEAMS_THREADS TEAMS THREADS

// Represents the total of threads in the Grid
#define XTEAM_TOTAL_NUM_THREADS (XTEAM_NUM_TEAMS * XTEAM_NUM_THREADS)

// Benchmark warmup iterations
#define WARMUP_ITERS 2
// Benchmark minimum number of measured iterations (as a lower bound in case the
// test is so slow that it wouldn't get enough iterations in the auto-scale
// timeframe)
#define BENCH_MIN_ITERS 10
// Auto-scale timeframe in seconds. Benchmarks will be repeated until they reach
// at least this amount of seconds.
#define AUTO_SCALE_TIME 1.0

// Floating point absolute and relative tolerance for comparison.
#define FP_ABS_TOL 1e-12
#define FP_REL_TOL 1e-6

// default alignment for aligned_alloc
#define ALIGNMENT 128

#define duration_cast(x)                                                       \
  std::chrono::duration_cast<std::chrono::duration<double>>(x)
using Clock = std::chrono::steady_clock;

struct TimingResult {
  double MinS, MaxS, AvgS;
  double BestMbps, AvgMbps;
};

// =========================================================================
// Utility functions
// =========================================================================

template <typename T> T *alloc(uint64_t N) {
  if (N > std::numeric_limits<size_t>::max() / sizeof(T)) {
    std::cerr << std::format("alloc size overflow n={} sizeof(T)={}\n", N,
                             sizeof(T));
    exit(EXIT_FAILURE);
  }
  size_t Bytes = sizeof(T) * N;
  Bytes = ((Bytes + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;

  T *Ret = static_cast<T *>(aligned_alloc(ALIGNMENT, Bytes));
  if (!Ret) {
    std::cerr << std::format("aligned_alloc failed bytes={}\n", Bytes);
    exit(EXIT_FAILURE);
  }
  return Ret;
}

template <typename T> T *targetAlloc(uint64_t N, int Devid) {
  if (N > std::numeric_limits<size_t>::max() / sizeof(T)) {
    std::cerr << std::format("target_alloc size overflow n={} sizeof(T)={}\n",
                             N, sizeof(T));
    exit(EXIT_FAILURE);
  }
  T *Ret = static_cast<T *>(omp_target_alloc(sizeof(T) * N, Devid));
  if (!Ret) {
    std::cerr << std::format("omp_target_alloc failed n={} devid={}\n", N,
                             Devid);
    exit(EXIT_FAILURE);
  }
  return Ret;
}

// The values are deterministic for reproducibility.
template <typename T, bool is_fp> void initData(T *Arr1, T *Arr2, uint64_t N) {
  srand(42);
  for (uint64_t I = 0; I < N; I++) {
    if constexpr (is_fp) {
      Arr1[I] = T((rand() % 100) / 100.0); // NOLINT(misc-predictable-rand)
      Arr2[I] = T((rand() % 100) / 100.0); // NOLINT(misc-predictable-rand)
    } else {
      Arr1[I] = T(rand() % 1000); // NOLINT(misc-predictable-rand)
      Arr2[I] = T(rand() % 1000); // NOLINT(misc-predictable-rand)
    }
  }
}

// =========================================================================
// Benchmark utilities
// =========================================================================

template <typename T, bool is_fp>
static bool check(T Result, T Gold, std::string_view Label) {
  if constexpr (!is_fp) {
    if (Result == Gold)
      return true;
    std::cerr << std::format("FAIL {}: got {}, expected {}\n", Label, Result,
                             Gold);
    return false;
  }
  double G = (double)Gold, C = (double)Result;
  double AbsErr = std::abs(C - G);
  double Scale = std::max({1.0, std::abs(G), std::abs(C)});
  double RelErr = AbsErr / Scale;
  if (AbsErr <= FP_ABS_TOL || RelErr <= FP_REL_TOL)
    return true;
  std::cerr << std::format("FAIL {}: got {}, expected {} (abs={}, rel={})\n",
                           Label, Result, Gold, AbsErr, RelErr);
  return false;
}

static TimingResult createTimingResult(const std::vector<double> &Times,
                                       uint64_t DataBytes) {
  if (Times.empty()) {
    std::cerr << "internal error: no timing samples collected\n";
    return TimingResult{0.0, 0.0, 0.0, 0.0, 0.0};
  }
  auto [Mn, Mx] = std::minmax_element(Times.begin(), Times.end());
  // NOLINTNEXTLINE(llvm-use-ranges)
  double Avg = std::accumulate(Times.begin(), Times.end(), 0.0) /
               static_cast<double>(Times.size());
  double BestMbps = (*Mn > 0.0) ? (1e-6 * DataBytes / *Mn) : 0.0;
  double AvgMbps = (Avg > 0.0) ? (1e-6 * DataBytes / Avg) : 0.0;
  return TimingResult{*Mn, *Mx, Avg, BestMbps, AvgMbps};
}

// Add locale-independent thousand separators to make visual number parsing
// easier
static std::string fmtNumSep(std::string S) {
  for (int Pos = S.length() - 3; Pos > 0; Pos -= 3)
    S.insert(Pos, ",");
  return S;
}

static void printResult(std::string_view Test, std::string_view Type,
                        uint64_t N, const std::optional<TimingResult> &R) {
  if (!R) {
    std::cerr << std::format("{:<24} {:<8} {:>15}  FAIL\n", Test, Type,
                             fmtNumSep(std::format("{}", N)));
    return;
  }
  std::cout << std::format("{:<24} {:<8} {:>15}  {:>10.6f}  {:>10.6f}  "
                           "{:>10.6f}  {:>12}  {:>12}\n",
                           Test, Type, fmtNumSep(std::format("{}", N)), R->MinS,
                           R->MaxS, R->AvgS,
                           fmtNumSep(std::format("{:.0f}", R->BestMbps)),
                           fmtNumSep(std::format("{:.0f}", R->AvgMbps)));
}

static void printHeader() {
  std::cout << std::format(
      "{:>24} {:>8} {:>15}  {:>10}  {:>10}  {:>10}  {:>12}  {:>12}\n", "test",
      "type", "N", "min(s)", "max(s)", "avg(s)", "best MB/s", "avg MB/s");
  std::cout << std::format(
      "{:->24} {:->8} {:->15}  {:->10}  {:->10}  {:->10}  {:->12}  {:->12}\n",
      "", "", "", "", "", "", "", "");
}
