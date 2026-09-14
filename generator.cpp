#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

void usage(const char* program) {
  std::cerr << "usage: " << program << " --size <n> [--seed <s>] [--output <file>]\n"
            << "  --size <n>       number of cells; accepts a K, M or G suffix (e.g. 10M)\n"
            << "  --seed <s>       seed of the random generator (default: 0)\n"
            << "  --output <file>  output file (default: automaton_<size>.txt)\n";
}

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "error: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

struct Unit {
  std::size_t factor;
  const char* suffix;
};

constexpr Unit units[] = {{1000000000, "G"}, {1000000, "M"}, {1000, "K"}};

/** Parse a number with an optional K, M or G suffix: "10M" is 10000000. */
std::size_t parse_size(std::string_view value) {
  std::size_t size = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, size);
  if (ec != std::errc{}) {
    fail("--size expects a non-negative integer, got '" + std::string(value) + "'");
  }
  const std::string_view suffix(ptr, last);
  if (!suffix.empty()) {
    const Unit* unit = nullptr;
    for (const Unit& candidate : units) {
      if (suffix == candidate.suffix) {
        unit = &candidate;
      }
    }
    if (unit == nullptr) {
      fail("--size accepts the suffixes K, M and G, got '" + std::string(suffix) + "'");
    }
    if (size > SIZE_MAX / unit->factor) {
      fail("--size overflows: '" + std::string(value) + "'");
    }
    size *= unit->factor;
  }
  return size;
}

std::size_t parse_seed(std::string_view value) {
  std::size_t seed = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, seed);
  if (ec != std::errc{} || ptr != last) {
    fail("--seed expects a non-negative integer, got '" + std::string(value) + "'");
  }
  return seed;
}

/** Name the file after the largest unit dividing the size exactly, so that
 * 10000000 gives "automaton_10M.txt" and 400 gives "automaton_400.txt". */
std::string default_name(std::size_t size) {
  for (const Unit& unit : units) {
    if (size >= unit.factor && size % unit.factor == 0) {
      return "automaton_" + std::to_string(size / unit.factor) + unit.suffix + ".txt";
    }
  }
  return "automaton_" + std::to_string(size) + ".txt";
}

/** Draw the cells 64 at a time: one call to the generator feeds 64 cells
 * instead of one, which matters when the size is in the millions. */
std::string random_cells(std::size_t size, std::size_t seed) {
  std::mt19937_64 generator(static_cast<std::uint64_t>(seed));
  std::string cells(size, '0');
  std::size_t i = 0;
  while (i < size) {
    std::uint64_t bits = generator();
    const std::size_t count = std::min<std::size_t>(64, size - i);
    for (std::size_t b = 0; b < count; ++b) {
      cells[i + b] = static_cast<char>('0' + (bits & 1));
      bits >>= 1;
    }
    i += count;
  }
  return cells;
}

int main(int argc, char** argv) {
  std::size_t size = 0;
  std::size_t seed = 0;
  std::string output;
  bool has_size = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--size" || arg == "--seed" || arg == "--output") {
      if (i + 1 == argc) {
        usage(argv[0]);
        fail("missing value after " + std::string(arg));
      }
      const std::string_view value(argv[++i]);
      if (arg == "--size") {
        size = parse_size(value);
        has_size = true;
      } else if (arg == "--seed") {
        seed = parse_seed(value);
      } else {
        output = value;
      }
    } else {
      usage(argv[0]);
      fail("unknown argument '" + std::string(arg) + "'");
    }
  }
  if (!has_size) {
    usage(argv[0]);
    fail("missing mandatory argument --size");
  }
  if (size == 0) {
    fail("--size must be at least 1");
  }
  if (output.empty()) {
    output = default_name(size);
  }

  const std::string cells = random_cells(size, seed);
  std::ofstream file(output);
  if (!file) {
    fail("cannot open '" + output + "' for writing");
  }
  file << size << '\n';
  file.write(cells.data(), static_cast<std::streamsize>(cells.size()));
  file << '\n';
  if (!file) {
    fail("cannot write '" + output + "'");
  }
  std::cerr << "wrote " << size << " cells to " << output << '\n';

  return 0;
}
