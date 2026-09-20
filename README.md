# Laboratory SIMD

Course: hardware acceleration.

## Case Study on Rule 110

* Generate three automaton (1KB, 1MB and 100MB):
```
g++ generator.cpp -O3 -o gen
./gen --size 1K
./gen --size 1M
./gen --size 100M
```
* For `rule110.cpp`, there are two compile options: `-DCELL_TYPE=int` or `-DCELL_TYPE=std::uint8_t`.
```
g++ rule110.cpp -O3 -march=native -Wall -Werror -Wextra --pedantic -std=c++23 -DNDEBUG -DCELL_TYPE=int -o rule110
./rule110 --init automaton_1K.txt --iter 100000
```
