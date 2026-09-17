// Increase (or decrease) the brightness of a binary PPM (P6) image.
// Usage: ./a.out [brightness] [input.ppm] [output.ppm] [repeat]
// Every argument is optional, the defaults are the ones below. `repeat` applies
// the brightness that many times, alternating the direction so the image does
// not run away, which gives a short benchmark enough work to measure.
//
// The type of a colour channel is picked at compile time, so the same source
// builds both versions of the benchmark: -DCHANNEL_TYPE=int against the default.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef CHANNEL_TYPE
  #define CHANNEL_TYPE std::uint8_t
#endif

#include <immintrin.h>

using channel_type = CHANNEL_TYPE;

struct RGB {
  channel_type R;
  channel_type G;
  channel_type B;
};

struct Image {
  int width;
  int height;
  std::vector<RGB> pixels;
};

Image read_ppm(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if(!file) {
    std::cerr << "Cannot open " << path << std::endl;
    exit(EXIT_FAILURE);
  }
  std::string magic;
  int width, height, maxval;
  file >> magic >> width >> height >> maxval;
  if(magic != "P6" || maxval != 255) {
    std::cerr << "Only binary PPM (P6) images with 255 as maximum value are supported." << std::endl;
    exit(EXIT_FAILURE);
  }
  file.get(); // Skip the single whitespace before the pixel data.
  Image image{width, height, std::vector<RGB>(width * height)};
  for(RGB& pixel : image.pixels) {
    pixel.R = file.get();
    pixel.G = file.get();
    pixel.B = file.get();
  }
  return image;
}

void write_ppm(const std::string& path, const Image& image) {
  std::ofstream file(path, std::ios::binary);
  if(!file) {
    std::cerr << "Cannot open " << path << std::endl;
    exit(EXIT_FAILURE);
  }
  file << "P6\n" << image.width << " " << image.height << "\n255\n";
  for(const RGB& pixel : image.pixels) {
    file.put(pixel.R);
    file.put(pixel.G);
    file.put(pixel.B);
  }
}

// One step of brightness: every component moves by `1`.
void brighten_by_one(std::vector<RGB>& pixels) {
  for(size_t i = 0; i < pixels.size(); ++i) {
    pixels[i].R += pixels[i].R == 255 ? 0 : 1;
    pixels[i].G += pixels[i].G == 255 ? 0 : 1;
    pixels[i].B += pixels[i].B == 255 ? 0 : 1;
  }
}

// The same step with AVX2 (2013): sixteen 256-bit registers, so 32 channels move
// per instruction against MMX's 8 and the scalar loop's 1.
//
// The kernel is one instruction. VPADDUSB adds bytes with unsigned saturation, so
// a channel already at 255 stays there by construction -- no comparison, no mask,
// no select. `x == 255 ? 0 : 1` is that instruction spelled out by hand.
//
// Channels are adjacent bytes and all get identical treatment, so the pixels are
// walked as a flat byte stream and the 3-byte stride never has to be reasoned
// about. Loads and stores are the unaligned forms: on any CPU with AVX2 they cost
// the same as the aligned ones when the address happens to be aligned, and a
// vector<RGB> gives no useful alignment guarantee anyway.
void brighten_by_one2(std::vector<RGB>& pixels) {
  if constexpr(sizeof(channel_type) != 1) {
    // A 256-bit register holds 32 bytes but only 8 ints, and the saturating add
    // does not exist for 32-bit lanes, so a wider channel type keeps the scalar
    // loop rather than a different and much less interesting kernel.
    for(size_t i = 0; i < pixels.size(); ++i) {
      pixels[i].R += pixels[i].R == 255 ? 0 : 1;
      pixels[i].G += pixels[i].G == 255 ? 0 : 1;
      pixels[i].B += pixels[i].B == 255 ? 0 : 1;
    }
  }
  else {
    std::uint8_t* data = reinterpret_cast<std::uint8_t*>(pixels.data());
    size_t channels = pixels.size() * 3;
    const __m256i one = _mm256_set1_epi8(1);
    size_t i = 0;
    for(; i + 32 <= channels; i += 32) {
      __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
      __m256i result = _mm256_adds_epu8(v, one);   // VPADDUSB, saturating at 255.
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(data + i), result);
    }
    for(; i < channels; ++i) {  // The last up-to-31 channels.
      data[i] += data[i] == 255 ? 0 : 1;
    }
  }
}

int main(int argc, char** argv) {
  int brightness = 50;
  std::string input = "large.ppm";
  std::string output = "output.ppm";
  int repeat = 1;
  if(argc > 5) {
    std::cerr << "Usage: " << argv[0]
              << " [brightness] [input.ppm] [output.ppm] [repeat]" << std::endl;
    return EXIT_FAILURE;
  }
  if(argc > 1) {
    brightness = std::stoi(argv[1]);
  }
  if(argc > 2) {
    input = argv[2];
  }
  if(argc > 3) {
    output = argv[3];
  }
  if(argc > 4) {
    repeat = std::stoi(argv[4]);
  }
  Image image = read_ppm(input);
  double megabytes = image.pixels.size() * sizeof(RGB) / (1024.0 * 1024.0);
  std::cout << image.width << "x" << image.height << " pixels, " << sizeof(RGB)
            << " bytes per pixel, " << megabytes << " MB in memory" << std::endl;
  auto start = std::chrono::steady_clock::now();
  for(int k = 0; k < repeat; ++k) {
    for(int i = 0; i < std::abs(brightness); ++i) {
#ifdef USE_AVX2
      brighten_by_one2(image.pixels);
#else
      brighten_by_one(image.pixels);
#endif
    }
  }
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "Brightness computed in " << elapsed.count() << " ms" << std::endl;
  write_ppm(output, image);
  return EXIT_SUCCESS;
}
