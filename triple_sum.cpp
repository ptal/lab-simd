#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

/** The matrix has three columns and `rows` rows: `matrix[i]` is the row `i`,
 * and `matrix[i][j]` the element of the row `i` in the column `j`. */
constexpr std::size_t columns = 3;

/** Number of rows such that the elements of the matrix weight about 500MB
 * (each row is allocated separately, so the real memory footprint is larger). */
constexpr std::size_t rows = 500ULL * 1000ULL * 1000ULL / (columns * sizeof(int));

std::vector<std::vector<int>> random_matrix() {
  std::vector<std::vector<int>> matrix(rows, std::vector<int>(columns));
  std::mt19937 generator(0);
  std::uniform_int_distribution<int> distribution(0, 100);
  for(std::size_t i = 0; i < matrix.size(); ++i) {
    for(std::size_t j = 0; j < columns; ++j) {
      matrix[i][j] = distribution(generator);
    }
  }
  return matrix;
}

void triple_sum(const std::vector<std::vector<int>>& matrix, std::vector<int>& sums) {
  for(std::size_t i = 0; i < sums.size(); ++i) {
    sums[i] = matrix[i][0] + matrix[i][1] + matrix[i][2];
  }
}

int main() {
  std::vector<std::vector<int>> matrix = random_matrix();
  std::vector<int> sums(rows);
  const auto start = std::chrono::steady_clock::now();
  triple_sum(matrix, sums);
  const std::chrono::duration<std::size_t, std::nano> elapsed =
    std::chrono::steady_clock::now() - start;
  /* We print a checksum of all the rows sums, otherwise the compiler could
   * simply delete the computation above. */
  std::uint64_t checksum = 0;
  for(std::size_t i = 0; i < sums.size(); ++i) {
    checksum += static_cast<std::uint64_t>(sums[i]);
  }
  std::cout << rows << " rows of " << columns << " columns ("
            << (rows * columns * sizeof(int)) / 1000 / 1000 << "MB of elements)\n";
  std::cout << "checksum: " << checksum << '\n';
  std::cout << elapsed.count() << "ns\n";
  std::cout << (double)elapsed.count()/1000./1000./1000. << "sec\n";
  return 0;
}
