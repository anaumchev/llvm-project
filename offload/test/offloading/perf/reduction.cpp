// OpenMP cross-team reduction performance & correctness benchmark
// Have a look at common.h for configuration options.

#include <numbers>
#include <unistd.h>

#include "omp.h"

#include "common.h"
#include "reduction.h"

static Config Conf;

template <typename T> static T redSum(const T *__restrict In, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(+ : S)
  for (uint64_t I = 0; I < N; I++)
    S += In[I];
  return S;
}

template <typename T> static T redMax(const T *__restrict In, uint64_t N) {
  T M = redIdentity<T, RedOp::Max>();
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(      \
        max : M)
  for (uint64_t I = 0; I < N; I++)
    M = std::max(M, In[I]);
  return M;
}

template <typename T> static T redMult(const T *__restrict In, uint64_t N) {
  T M = redIdentity<T, RedOp::Mult>();
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(* : M)
  for (uint64_t I = 0; I < N; I++)
    M *= In[I];
  return M;
}

template <typename T>
static T redDot(const T *__restrict A, const T *__restrict B, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(+ : S)
  for (uint64_t I = 0; I < N; I++)
    S += A[I] * B[I];
  return S;
}

// Reduction through an indirect call, checking for potential frontend pattern
// matching.
template <typename T> static T redIndirect(const T *__restrict In, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();
  auto Accumulate = [](T A, T B) { return A + B; };
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(+ : S)
  for (uint64_t I = 0; I < N; I++)
    S = Accumulate(S, In[I]);
  return S;
}

// Combined reduction (sum and max) in the same loop.
template <typename T> static T redCombined(const T *__restrict In, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();
  T M = redIdentity<T, RedOp::Max>();
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(      \
        + : S) reduction(max : M)
  for (uint64_t I = 0; I < N; I++) {
    S += In[I];
    M = std::max(M, In[I]);
  }
  return (S / 2) + (M / 2);
}

// Combined reduction (sum and max) in separate loops.
template <typename T>
static T redCombinedSeparate(const T *__restrict In, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();
  T M = redIdentity<T, RedOp::Max>();
#pragma omp target map(tofrom : S, M)
#pragma omp teams TEAMS reduction(+ : S) reduction(max : M)
  {
#pragma omp distribute parallel for THREADS reduction(+ : S)
    for (uint64_t I = 0; I < N; I++)
      S += In[I];

#pragma omp distribute parallel for THREADS reduction(max : M)
    for (uint64_t I = 0; I < N; I++)
      M = std::max(M, In[I]);
  }
  return (S / 2) + (M / 2);
}

// Have a reduction in a kernel that is also doing something completely
// unrelated to the reduction (pure register work, no memory ops).
template <typename T>
static T redKernelPart(const T *__restrict In, uint64_t N) {
  T S = redIdentity<T, RedOp::Sum>();

#pragma omp target map(tofrom : S)
#pragma omp teams TEAMS reduction(+ : S)
  {
#pragma omp distribute parallel for THREADS reduction(+ : S)
    for (uint64_t I = 0; I < N; I++)
      S += In[I];

    // Just do something, without actually doing anything
#pragma omp parallel THREADS
    {
      int TID = omp_get_thread_num();
      T X = static_cast<T>(TID);
      for (int J = 0; J < 100; J++)
        X = X * static_cast<T>(0.9) + static_cast<T>(J);
      if (X == static_cast<T>(-1))
        S += X;
    }
  }

  return S;
}

// Reduction without any memory access.
static double redPi(uint64_t N) {
  double Pi = 0.0;

  // https://en.wikipedia.org/wiki/Leibniz_formula_for_%CF%80
#pragma omp target teams distribute parallel for TEAMS_THREADS reduction(+ : Pi)
  for (uint64_t I = 0; I < N; I++) {
    double Term = 1.0 / (2 * I + 1);
    Pi += (I & 0x1) ? -Term : Term;
  }

  return Pi * 4.0;
}

// =========================================================================
// Benchmark utilities
// =========================================================================

template <typename T, bool is_fp, typename KernelFunc, typename... InputArgs>
static std::optional<TimingResult>
runBenchRed(KernelFunc Kernel, T Gold, uint64_t N, std::string_view Label,
            InputArgs... Inputs) {
  std::vector<double> Times;
  double TotalTime = 0.0;
  int WarmUpIters = WARMUP_ITERS;

  while (TotalTime < AUTO_SCALE_TIME || Times.size() < BENCH_MIN_ITERS) {
    auto T1 = Clock::now();
    T Result = Kernel(Inputs..., N);
    auto T2 = Clock::now();
    if (!check<T, is_fp>(Result, Gold, Label))
      return std::nullopt;

    if (WarmUpIters > 0) {
      WarmUpIters--;
      continue;
    }
    double D = duration_cast(T2 - T1).count();
    Times.push_back(D);
    TotalTime += D;
  }

  return createTimingResult(Times, sizeof(T) * N * sizeof...(Inputs));
}

// Run the benchmarks for the given data type (is_fp: true for floating point
// types).
template <typename T, bool is_fp>
static void runType(std::string_view TypeName) {
  std::optional<TimingResult> R;

  for (uint64_t N : Conf.ArraySizes) {
    T *In1 = alloc<T>(N);
    T *In2 = alloc<T>(N);
    initData<T, is_fp>(In1, In2, N);

#pragma omp target enter data map(to : In1[0 : N], In2[0 : N])

    T GoldSum = goldRed<T, RedOp::Sum>(In1, N);
    T GoldMax = goldRed<T, RedOp::Max>(In1, N);
    T GoldMult = goldRed<T, RedOp::Mult>(In1, N);
    T GoldDot = goldRedDot<T>(In1, In2, N);

    // ================================================================
    // dot reduction
    // ================================================================
    R = runBenchRed<T, is_fp>(redDot<T>, GoldDot, N, "red_dot", In1, In2);
    printResult("red_dot", TypeName, N, R);

    // ================================================================
    // max reduction
    // ================================================================
    R = runBenchRed<T, is_fp>(redMax<T>, GoldMax, N, "red_max", In1);
    printResult("red_max", TypeName, N, R);

    // ================================================================
    // sum reduction
    // ================================================================
    R = runBenchRed<T, is_fp>(redSum<T>, GoldSum, N, "red_sum", In1);
    printResult("red_sum", TypeName, N, R);

    if (!Conf.QuickRun || std::is_same_v<T, double>) {
      // ================================================================
      // mult reduction
      // ================================================================
      R = runBenchRed<T, is_fp>(redMult<T>, GoldMult, N, "red_mult", In1);
      printResult("red_mult", TypeName, N, R);

      // ================================================================
      // indirect reduction (sum)
      // ================================================================
      R = runBenchRed<T, is_fp>(redIndirect<T>, GoldSum, N, "red_indirect",
                                In1);
      printResult("red_indirect", TypeName, N, R);

      // ================================================================
      // combined (sum and max) reduction - in the same loop ...
      // ================================================================
      T GoldCombined = (GoldSum / 2) + (GoldMax / 2);
      R = runBenchRed<T, is_fp>(redCombined<T>, GoldCombined, N, "red_combined",
                                In1);
      printResult("red_combined", TypeName, N, R);

      // ================================================================
      // ... and in separate loops
      // ================================================================
      R = runBenchRed<T, is_fp>(redCombinedSeparate<T>, GoldCombined, N,
                                "red_combined_separate", In1);
      printResult("red_combined_separate", TypeName, N, R);

      // ================================================================
      // reduction (sum) in a kernel that is also doing something completely
      // unrelated to the reduction.
      // ================================================================
      R = runBenchRed<T, is_fp>(redKernelPart<T>, GoldSum, N, "red_kernel_part",
                                In1);
      printResult("red_kernel_part", TypeName, N, R);
    }

#pragma omp target exit data map(delete : In1[0 : N], In2[0 : N])

    free(In1);
    free(In2);
  }

  if (std::is_same_v<T, double>) {
    // ================================================================
    // reduction computing Pi
    // ================================================================
    double GoldPi = std::numbers::pi;
    uint64_t N = 5000000000;
    R = runBenchRed<double, true>(redPi, GoldPi, N, "red_pi");
    printResult("red_pi", TypeName, N, R);
  }
}

static void usage(std::string_view Argv0) {
  std::cout << "Usage: " << Argv0 << " [-v] [-h]\n"
            << "  -v: Verbose run (test all array sizes)\n"
            << "  -h: Show this help message\n";
}

int main(int argc, char *const *argv) {
  int Opt;

  while ((Opt = getopt(argc, argv, "vh")) != -1) {
    switch (Opt) {
    case 'v':
      Conf.QuickRun = false;
      break;
    case 'h':
      usage(argv[0]);
      return EXIT_SUCCESS;
    default:
      usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (Conf.QuickRun)
    Conf.ArraySizes.assign(ArraySizesQuick.begin(), ArraySizesQuick.end());
  else
    Conf.ArraySizes.assign(ArraySizes.begin(), ArraySizes.end());

  std::cout << std::format(
      "xteam reduction benchmark (quick run: {}) — {} warmup iterations "
      "{} teams, {} threads, codegen autodetection: {}\n",
      Conf.QuickRun ? "true" : "false", WARMUP_ITERS, XTEAM_NUM_TEAMS,
      XTEAM_NUM_THREADS, CODEGEN_AUTODETECTION ? "true" : "false");

  std::cout << "Array sizes: ";
  for (uint64_t SZ : Conf.ArraySizes)
    std::cout << " " << fmtNumSep(std::format("{}", SZ));
  std::cout << "\n\n";

  printHeader();

  std::cout << "\n--- double ---\n";
  runType<double, true>("double");

  std::cout << "\n--- uint ---\n";
  runType<unsigned, false>("uint");

  std::cout << "\n--- ulong ---\n";
  runType<unsigned long, false>("ulong");

  return EXIT_SUCCESS;
}
