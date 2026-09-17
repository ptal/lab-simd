// Extended with Claude Opus 5.1, based on https://github.com/lemire/Code-used-on-Daniel-Lemire-s-blog/tree/master/2024/01/13

// Measure the DRAM bandwidth of this machine, which is the ceiling a roofline
// argument needs: a kernel at 30 GB/s is only "memory bound" next to the number
// this program prints.
//
// Usage: ./bandwidth [GiB] [repeats] [max threads] [kernel]
//
// Three things make the difference between a bandwidth number and a correct one.
//
// 1. One core cannot saturate DRAM. A core can only have ~10-16 cache line fills
//    in flight (its line fill buffers), so by Little's law one core gets about
//    12 lines x 64 B / 80 ns ~ 10 GB/s no matter how fast the memory is. Only a
//    sweep over thread counts finds the real ceiling, so that is what we print.
//
// 2. Threads must be pinned, one per physical core first. Left to the scheduler,
//    two threads land on the two hyperthreads of one core, share that core's fill
//    buffers, and the curve flattens for a reason that has nothing to do with DRAM.
//
// 3. "Bandwidth" depends on the read/write mix, so one number is not enough:
//    - read      loads only, the cheapest traffic there is;
//    - rmw       a[i] += 1, what brightness.cpp does: the store needs the line,
//                so DRAM sees one read plus one writeback per byte;
//    - ntwrite   non-temporal stores, which skip the read-for-ownership and are
//                the only way to write memory at one byte of traffic per byte;
//    - copy      b[i] = a[i]: read a, read b to own it, write b back -- three
//                bytes of DRAM traffic per byte copied.
//    Every column is DRAM traffic per second, the multipliers above included, so
//    the columns are comparable to each other and to a perf measurement.
//
// Those multipliers are a model, and the only thing that can confirm them is the
// memory controller. Beware of checking them with the L2 counters instead: this
// machine has a non-inclusive victim L3, so a *clean* line evicted from L2 is
// pushed into L3 and counted by l2_trans.l2_wb although nothing goes to memory,
// and (l2_lines_in.all + l2_trans.l2_wb) x 64 reports twice the traffic a
// read-only pass really moves. Non-temporal stores are the opposite error: they
// never enter L2 and the counters see none of them. The formula is only right
// when every line is read, dirtied and written back, which is what brightness.cpp
// does -- that is why it works there and not here. The controller counters need
// root (uncore events are refused at kernel.perf_event_paranoid = 2):
//
//   sudo perf stat -a -e uncore_imc/cas_count_read/,uncore_imc/cas_count_write/
//        ./bandwidth 4 3 10 read
//
// Each CAS is 64 bytes; if the aggregate alias is missing, sum uncore_imc_0..5.
//
// Compile with: g++ bandwidth.cpp -O3 -march=native -std=c++20 -pthread -o bandwidth

#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <immintrin.h>
#include <pthread.h>
#include <sys/mman.h>

using clock_type = std::chrono::steady_clock;

volatile uint64_t g_sink = 0;

// A CPU list with one hyperthread per physical core first, then the siblings, so
// that a sweep of n threads uses n distinct cores before it doubles up on one.
std::vector<int> cpu_order() {
  std::vector<int> cores, siblings;
  std::set<int> seen;
  for(int cpu = 0; cores.size() + siblings.size() < std::thread::hardware_concurrency(); ++cpu) {
    std::ifstream file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id");
    int core;
    if(!(file >> core)) { continue; }
    (seen.insert(core).second ? cores : siblings).push_back(cpu);
  }
  cores.insert(cores.end(), siblings.begin(), siblings.end());
  return cores;
}

void pin(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

// Huge pages, or 4 KiB pages cost a page walk every 64 accesses on a stride-64
// read and the measurement is partly a TLB benchmark.
uint8_t* allocate(size_t bytes) {
  void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(memory == MAP_FAILED) {
    std::perror("mmap");
    exit(EXIT_FAILURE);
  }
  madvise(memory, bytes, MADV_HUGEPAGE);
  return static_cast<uint8_t*>(memory);
}

// One byte per cache line is enough to pull the whole line from DRAM, and leaves
// the core idle enough that we measure the memory system and not the loop.
template <bool prefetch>
void read(uint8_t* a, uint8_t*, size_t n) {
  uint64_t sum = 0;
  for(size_t i = 0; i < n; i += 64) {
    sum += a[i];
    if(prefetch) { __builtin_prefetch(&a[i + 4096], 0, 3); }
  }
  g_sink += sum;
}

void rmw(uint8_t* a, uint8_t*, size_t n) {
  for(size_t i = 0; i < n; ++i) {
    a[i] += 1;
  }
}

void ntwrite(uint8_t* a, uint8_t*, size_t n) {
  __m256i value = _mm256_set1_epi8(1);
  for(size_t i = 0; i + 32 <= n; i += 32) {
    _mm256_stream_si256(reinterpret_cast<__m256i*>(a + i), value);
  }
  _mm_sfence();
}

void copy(uint8_t* a, uint8_t* b, size_t n) {
  for(size_t i = 0; i + 32 <= n; i += 32) {
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(b + i),
                        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i)));
  }
}

struct Kernel {
  const char* name;
  double traffic;  // DRAM bytes moved per byte of the array the kernel walks.
  void (*run)(uint8_t*, uint8_t*, size_t);
};

const Kernel KERNELS[] = {
  {"read",     1.0, read<false>},
  {"read+pf",  1.0, read<true>},
  {"rmw",      2.0, rmw},
  {"ntwrite",  1.0, ntwrite},
  {"copy",     3.0, copy},
};

int main(int argc, char** argv) {
  double gibibytes = argc > 1 ? std::stod(argv[1]) : 4.0;
  int repeats = argc > 2 ? std::stoi(argv[2]) : 3;
  std::vector<int> cpus = cpu_order();
  size_t max_threads = argc > 3 ? std::stoul(argv[3]) : cpus.size();
  // One kernel only, for running a single loop under perf.
  std::string only = argc > 4 ? argv[4] : "";

  size_t volume = static_cast<size_t>(gibibytes * 1024 * 1024 * 1024);
  volume -= volume % (max_threads * 64);  // whole cache lines per thread
  bool needs_b = only.empty() || only == "copy";
  uint8_t* a = allocate(volume);
  uint8_t* b = needs_b ? allocate(volume) : a;

  // First touch from the threads that will read the pages, which is what decides
  // the NUMA node a page lands on. One node here, but the habit is free.
  {
    std::vector<std::thread> touch;
    size_t chunk = volume / cpus.size();
    for(size_t i = 0; i < cpus.size(); ++i) {
      touch.emplace_back([=] {
        pin(cpus[i]);
        std::memset(a + i * chunk, 1, chunk);
        if(needs_b) { std::memset(b + i * chunk, 1, chunk); }
      });
    }
    for(std::thread& t : touch) { t.join(); }
  }

  std::printf("%.1f GiB per pass, best of %d, DRAM traffic in GB/s\n\n", volume / 1073741824.0, repeats);
  std::printf("%8s", "threads");
  for(const Kernel& kernel : KERNELS) {
    if(only.empty() || only == kernel.name) { std::printf("%10s", kernel.name); }
  }
  std::printf("\n");

  for(size_t threads = 1; threads <= max_threads; ++threads) {
    size_t chunk = (volume / threads) & ~size_t(63);
    const Kernel* current = nullptr;
    bool stop = false;
    std::barrier gate(threads + 1);
    std::vector<std::thread> workers;
    for(size_t i = 0; i < threads; ++i) {
      workers.emplace_back([&, i] {
        pin(cpus[i]);
        while(true) {
          gate.arrive_and_wait();  // Wait for the next kernel.
          if(stop) { return; }
          current->run(a + i * chunk, b + i * chunk, chunk);
          gate.arrive_and_wait();  // Report it done.
        }
      });
    }

    // The threads exist and the pages are warm, so the timer brackets the kernel
    // and nothing else -- no thread creation, no page faults, no allocation.
    auto measure = [&](const Kernel& kernel) {
      current = &kernel;
      gate.arrive_and_wait();
      auto start = clock_type::now();
      gate.arrive_and_wait();
      std::chrono::duration<double> elapsed = clock_type::now() - start;
      return kernel.traffic * chunk * threads / elapsed.count() / 1e9;
    };

    std::printf("%8zu", threads);
    for(const Kernel& kernel : KERNELS) {
      if(!only.empty() && only != kernel.name) { continue; }
      measure(kernel);  // Warm-up: the uncore clocks up before we believe a number.
      double best = 0;
      for(int r = 0; r < repeats; ++r) { best = std::max(best, measure(kernel)); }
      std::printf("%10.1f", best);
      std::fflush(stdout);
    }
    std::printf("\n");

    stop = true;
    gate.arrive_and_wait();
    for(std::thread& t : workers) { t.join(); }
  }
  return EXIT_SUCCESS;
}
