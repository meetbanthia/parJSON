#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

struct ChunkState {
  bool in_string = false;
  bool odd_backslash_run = false;
};

struct ChunkSimulation {
  ChunkState outgoing[2][2];
  std::vector<unsigned char> masks[2][2];
};

std::string read_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Unable to open input file: " + path);
  }

  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  input.seekg(0, std::ios::beg);

  std::string contents(static_cast<std::size_t>(size), '\0');
  input.read(contents.data(), size);
  if (!input && !input.eof()) {
    throw std::runtime_error("Failed while reading input file: " + path);
  }

  return contents;
}

void write_output(const std::string &path, const std::vector<unsigned char> &mask) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Unable to open output file: " + path);
  }

  for (unsigned char bit : mask) {
    output.put(static_cast<char>('0' + bit));
  }
  output.put('\n');
}

ChunkSimulation simulate_chunk(const std::string &data, std::size_t begin, std::size_t end) {
  ChunkSimulation simulation;
  const std::size_t chunk_size = end - begin;

  for (int in_string_seed = 0; in_string_seed < 2; ++in_string_seed) {
    for (int odd_backslash_seed = 0; odd_backslash_seed < 2; ++odd_backslash_seed) {
      auto &mask = simulation.masks[in_string_seed][odd_backslash_seed];
      mask.assign(chunk_size, 0);

      bool in_string = static_cast<bool>(in_string_seed);
      bool odd_backslash_run = static_cast<bool>(odd_backslash_seed);

      for (std::size_t offset = 0; offset < chunk_size; ++offset) {
        const char c = data[begin + offset];

        if (c == '\\') {
          mask[offset] = static_cast<unsigned char>(in_string ? 1 : 0);
          odd_backslash_run = !odd_backslash_run;
          continue;
        }

        const bool escaped_quote = (c == '"') && odd_backslash_run;
        odd_backslash_run = false;

        if (c == '"' && !escaped_quote) {
          in_string = !in_string;
          mask[offset] = 0;
          continue;
        }

        mask[offset] = static_cast<unsigned char>(in_string ? 1 : 0);
      }

      simulation.outgoing[in_string_seed][odd_backslash_seed] = {in_string, odd_backslash_run};
    }
  }

  return simulation;
}

std::vector<unsigned char> build_mask(const std::string &data, std::size_t chunk_size) {
  if (chunk_size == 0) {
    throw std::runtime_error("Chunk size must be greater than zero.");
  }

  const std::size_t length = data.size();
  std::vector<unsigned char> mask(length, 0);
  if (length == 0) {
    return mask;
  }

  const std::size_t chunk_count = (length + chunk_size - 1) / chunk_size;
  std::vector<ChunkSimulation> chunk_sims(chunk_count);
  std::vector<ChunkState> incoming_states(chunk_count);

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t begin = static_cast<std::size_t>(chunk_index) * chunk_size;
    const std::size_t end = std::min(begin + chunk_size, length);
    chunk_sims[static_cast<std::size_t>(chunk_index)] = simulate_chunk(data, begin, end);
  }

  ChunkState state{};
  for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
    incoming_states[chunk_index] = state;
    const auto &chunk = chunk_sims[chunk_index];
    state = chunk.outgoing[state.in_string ? 1 : 0][state.odd_backslash_run ? 1 : 0];
  }

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t current = static_cast<std::size_t>(chunk_index);
    const std::size_t begin = current * chunk_size;
    const auto &state_for_chunk = incoming_states[current];
    const auto &chunk_mask =
        chunk_sims[current].masks[state_for_chunk.in_string ? 1 : 0]
                                [state_for_chunk.odd_backslash_run ? 1 : 0];

    for (std::size_t offset = 0; offset < chunk_mask.size(); ++offset) {
      mask[begin + offset] = chunk_mask[offset];
    }
  }

  return mask;
}

void print_usage(const char *program_name) {
  std::cerr << "Usage: " << program_name
            << " <input.json> [output.txt] [chunk_size] [num_threads]\n"
            << "Produces a 0/1 mask where positions inside JSON strings are 1.\n";
}

}  // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 5) {
      print_usage(argv[0]);
      return 1;
    }

    const std::string input_path = argv[1];
    const std::string output_path = argc >= 3 ? argv[2] : "";
    const std::size_t chunk_size = argc >= 4 ? std::stoull(argv[3]) : 1 << 14;
    const int num_threads = argc >= 5 ? std::stoi(argv[4]) : 0;

    if (argc >= 5 && num_threads <= 0) {
      throw std::runtime_error("Number of threads must be greater than zero.");
    }

#ifdef _OPENMP
    if (num_threads > 0) {
      omp_set_num_threads(num_threads);
    }
#else
    if (num_threads > 0) {
      std::cerr << "Warning: num_threads was provided, but this build does not include OpenMP "
                   "support.\n";
    }
#endif

    const std::string data = read_file(input_path);
    const auto mask = build_mask(data, chunk_size);

    if (output_path.empty()) {
      for (unsigned char bit : mask) {
        std::cout << static_cast<char>('0' + bit);
      }
      std::cout << '\n';
    } else {
      write_output(output_path, mask);
    }

#ifdef _OPENMP
    std::cerr << "Processed " << data.size() << " bytes with " << omp_get_max_threads()
              << " OpenMP threads.\n";
#else
    std::cerr << "Processed " << data.size() << " bytes without OpenMP support.\n";
#endif
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
