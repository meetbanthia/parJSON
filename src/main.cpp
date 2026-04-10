#include <exception>
#include <iostream>
#include <string>

#include <omp.h>

#include "parjson.hpp"

int main(int argc, char **argv) {
  try {
    const parjson::Config config = parjson::parse_arguments(argc, argv);

    if (config.chunk_size == 0) {
      throw std::runtime_error("Chunk size must be greater than zero.");
    }
    if (config.num_threads < 0) {
      throw std::runtime_error("Number of threads cannot be negative.");
    }

    if (config.num_threads > 0) {
      omp_set_num_threads(config.num_threads);
    }
    const int active_threads =
        config.strategy == parjson::Strategy::Sequential
            ? 1
            : (config.num_threads > 0 ? config.num_threads : omp_get_max_threads());

    const std::string data = parjson::read_file(config.input_path);
    const parjson::ParseResult result = parjson::parse_json(data, config);

    std::string output;
    if (config.mode == parjson::OutputMode::Mask) {
      output = parjson::render_mask(result.string_mask);
    } else if (config.mode == parjson::OutputMode::MaskBenchmark) {
      output = parjson::render_mask_benchmark_json(data.size(), config.strategy, active_threads,
                                                   config.mask_algorithm, result.mask_build_seconds);
      output.push_back('\n');
    } else if (config.mode == parjson::OutputMode::Summary) {
      output = parjson::render_summary_json(result.summary, config.strategy, config.chunk_size,
                                            active_threads, config.mask_algorithm);
      output.push_back('\n');
    } else {
      output = parjson::render_index_json(result.summary, config.strategy, config.chunk_size,
                                          active_threads, config.mask_algorithm);
      output.push_back('\n');
    }

    if (config.output_path.empty()) {
      std::cout << output;
    } else {
      parjson::write_text(config.output_path, output);
    }

    std::cerr << "Processed " << data.size() << " bytes with strategy="
              << (config.strategy == parjson::Strategy::Sequential ? "seq" : "omp")
              << " threads=" << active_threads << ".\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
