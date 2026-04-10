#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace parjson {

enum class Strategy {
  Sequential,
  OpenMP,
};

enum class MaskAlgorithm {
  Algo1,
  Algo2,
  Algo3,
};

enum class OutputMode {
  Mask,
  MaskBenchmark,
  Summary,
  Index,
};

struct ParseSummary {
  std::size_t bytes = 0;
  std::size_t string_characters = 0;
  std::size_t string_literals = 0;
  std::size_t number_literals = 0;
  std::size_t true_literals = 0;
  std::size_t false_literals = 0;
  std::size_t null_literals = 0;
  std::size_t object_opens = 0;
  std::size_t array_opens = 0;
  std::size_t structural_characters = 0;
  std::size_t colon_count = 0;
  std::size_t comma_count = 0;
  std::size_t max_depth = 0;
  std::vector<std::size_t> structural_indexes;
};

struct ParseResult {
  std::vector<unsigned char> string_mask;
  ParseSummary summary;
  double mask_build_seconds = 0.0;
};

struct Config {
  std::string input_path;
  std::string output_path;
  std::size_t chunk_size = 1 << 14;
  int num_threads = 0;
  Strategy strategy = Strategy::OpenMP;
  MaskAlgorithm mask_algorithm = MaskAlgorithm::Algo2;
  OutputMode mode = OutputMode::Summary;
};

std::string mask_algorithm_name(Strategy strategy, MaskAlgorithm mask_algorithm);
std::string read_file(const std::string &path);
void write_text(const std::string &path, const std::string &contents);

ParseResult parse_json(const std::string &data, const Config &config);

std::string render_mask(const std::vector<unsigned char> &mask);
std::string render_mask_benchmark_json(std::size_t bytes, Strategy strategy, int threads,
                                       MaskAlgorithm mask_algorithm, double mask_build_seconds);
std::string render_summary_json(const ParseSummary &summary, Strategy strategy, std::size_t chunk_size,
                                int threads, MaskAlgorithm mask_algorithm);
std::string render_index_json(const ParseSummary &summary, Strategy strategy, std::size_t chunk_size,
                              int threads, MaskAlgorithm mask_algorithm);

void print_usage(const char *program_name);
OutputMode parse_mode(const std::string &value);
Strategy parse_strategy(const std::string &value);
MaskAlgorithm parse_mask_algorithm(const std::string &value);
Config parse_flag_arguments(int argc, char **argv);
Config parse_legacy_arguments(int argc, char **argv);
Config parse_arguments(int argc, char **argv);

}  // namespace parjson
