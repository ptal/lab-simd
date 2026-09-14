#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <ranges>

#ifndef CELL_TYPE
#define CELL_TYPE int
#endif

using cell_type = CELL_TYPE;

struct Config {
  std::size_t iterations = 0;
  std::string init_file;
};

void usage(const char* program) {
  std::cerr << "usage: " << program << " --iter <n> --init <file>\n"
            << "  --iter <n>     number of iterations of the simulation loop\n"
            << "  --init <file>  file containing the initial configuration of the automaton:\n"
            << "                 the first line is the size, the second line the cells\n";
}

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "error: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

Config parse_args(int argc, char** argv) {
  Config config;
  bool has_iter = false;
  bool has_init = false;
  if (argc != 5) {
    usage(argv[0]);
    fail("missing arguments");
  }
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg == "--iter" || arg == "--init") {
      std::string_view value(argv[++i]);
      if (arg == "--iter") {
        const char* first = value.data();
        const char* last = value.data() + value.size();
        const auto [ptr, ec] = std::from_chars(first, last, config.iterations);
        if (ec != std::errc{} || ptr != last) {
          fail("--iter expects a non-negative integer, got '" + std::string(value) + "'");
        }
        has_iter = true;
      } else {
        config.init_file = value;
        has_init = true;
      }
    } else {
      usage(argv[0]);
      fail("unknown argument '" + std::string(arg) + "'");
    }
  }
  if (!has_iter || !has_init) {
    usage(argv[0]);
    fail(std::string("missing mandatory argument ") + (has_iter ? "--init" : "--iter"));
  }
  return config;
}

/** Read the initial configuration: the first line is the number of cells, the
 * second line the cells themselves, one character ('0' or '1') per cell. */
std::vector<cell_type> read_automaton(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    fail("cannot open '" + path + "'");
  }
  std::size_t size = 0;
  if (!(file >> size)) {
    fail("cannot read the size of the automaton in '" + path + "'");
  }
  std::string cells;
  cells.reserve(size);
  if (!(file >> cells)) {
    fail("cannot read the initial configuration in '" + path + "'");
  }
  if (cells.size() != size) {
    fail("'" + path + "' announces " + std::to_string(size) + " cells but contains " +
         std::to_string(cells.size()));
  }
  std::vector<cell_type> automaton(size);
  for (std::size_t i = 0; i < size; ++i) {
    if (cells[i] != '0' && cells[i] != '1') {
      fail("'" + path + "' contains the invalid cell '" + cells[i] + "' at position " +
           std::to_string(i));
    }
    automaton[i] = static_cast<cell_type>(cells[i] - '0');
  }
  return automaton;
}

void simulate(const Config& config, std::vector<cell_type>& current, std::vector<cell_type>& next) {
  for(size_t k = 0; k < config.iterations; ++k) {
    for(size_t i = 1; i < current.size() - 1; ++i) {
      if(current[i-1] && current[i] && current[i+1]) next[i] = 0;
      else if(current[i-1] && current[i] && !current[i+1]) next[i] = 1;
      else if(current[i-1] && !current[i] && current[i+1]) next[i] = 1;
      else if(current[i-1] && !current[i] && !current[i+1]) next[i] = 0;
      else if(!current[i-1] && current[i] && current[i+1]) next[i] = 1;
      else if(!current[i-1] && current[i] && !current[i+1]) next[i] = 1;
      else if(!current[i-1] && !current[i] && current[i+1]) next[i] = 1;
      else next[i] = 0;
    }
    std::swap(next,current);
    next.front() = 0;
    next.back() = 0;
  }
}

int main(int argc, char** argv) {
  Config config = parse_args(argc, argv);
  std::vector<cell_type> current = read_automaton(config.init_file);
  std::vector<cell_type> next(current.size());
  next.front() = 0;
  next.back() = 0;
  const auto start = std::chrono::steady_clock::now();
  simulate(config, current, next);
  const std::chrono::duration<size_t, std::nano> elapsed =
    std::chrono::steady_clock::now() - start;
  std::cout << std::ranges::fold_left_first(current, std::plus<int>()).value() << std::endl;
  std::cout << elapsed.count() << "ns\n";
  std::cout << (double)elapsed.count()/1000./1000./1000. << "sec\n";
  return 0;
}
