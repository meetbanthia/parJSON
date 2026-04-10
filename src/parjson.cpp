#include "parjson.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace parjson {

std::string mask_algorithm_name(Strategy strategy, MaskAlgorithm mask_algorithm) {
  if (strategy == Strategy::Sequential) {
    return "sequential";
  }

  switch (mask_algorithm) {
    case MaskAlgorithm::Algo1:
      return "algo1";
    case MaskAlgorithm::Algo2:
      return "algo2";
    case MaskAlgorithm::Algo3:
      return "algo3";
  }

  throw std::runtime_error("Unsupported mask algorithm.");
}

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

void write_text(const std::string &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Unable to open output file: " + path);
  }
  output << contents;
}

std::string render_mask(const std::vector<unsigned char> &mask) {
  std::string output;
  output.reserve(mask.size() + 1);
  for (unsigned char bit : mask) {
    output.push_back(static_cast<char>('0' + bit));
  }
  output.push_back('\n');
  return output;
}

std::string render_mask_benchmark_json(std::size_t bytes, Strategy strategy, int threads,
                                       MaskAlgorithm mask_algorithm, double mask_build_seconds) {
  std::string json = "{\n";
  json += "  \"mode\": \"mask-benchmark\",\n";
  json += "  \"strategy\": \"" +
          std::string(strategy == Strategy::Sequential ? "sequential" : "openmp") + "\",\n";
  json += "  \"mask_algorithm\": \"" + mask_algorithm_name(strategy, mask_algorithm) + "\",\n";
  json += "  \"threads\": " + std::to_string(threads) + ",\n";
  json += "  \"bytes\": " + std::to_string(bytes) + ",\n";
  json += "  \"mask_build_seconds\": " + std::to_string(mask_build_seconds) + "\n";
  json += "}";
  return json;
}

std::string render_summary_json(const ParseSummary &summary, Strategy strategy, std::size_t chunk_size,
                                int threads, MaskAlgorithm mask_algorithm) {
  std::string json = "{\n";
  json += "  \"strategy\": \"" +
          std::string(strategy == Strategy::Sequential ? "sequential" : "openmp") + "\",\n";
  json += "  \"mask_algorithm\": \"" + mask_algorithm_name(strategy, mask_algorithm) + "\",\n";
  json += "  \"threads\": " + std::to_string(threads) + ",\n";
  json += "  \"chunk_size\": " + std::to_string(chunk_size) + ",\n";
  json += "  \"bytes\": " + std::to_string(summary.bytes) + ",\n";
  json += "  \"string_characters\": " + std::to_string(summary.string_characters) + ",\n";
  json += "  \"string_literals\": " + std::to_string(summary.string_literals) + ",\n";
  json += "  \"number_literals\": " + std::to_string(summary.number_literals) + ",\n";
  json += "  \"true_literals\": " + std::to_string(summary.true_literals) + ",\n";
  json += "  \"false_literals\": " + std::to_string(summary.false_literals) + ",\n";
  json += "  \"null_literals\": " + std::to_string(summary.null_literals) + ",\n";
  json += "  \"object_opens\": " + std::to_string(summary.object_opens) + ",\n";
  json += "  \"array_opens\": " + std::to_string(summary.array_opens) + ",\n";
  json += "  \"colon_count\": " + std::to_string(summary.colon_count) + ",\n";
  json += "  \"comma_count\": " + std::to_string(summary.comma_count) + ",\n";
  json += "  \"structural_characters\": " + std::to_string(summary.structural_characters) + ",\n";
  json += "  \"max_depth\": " + std::to_string(summary.max_depth) + "\n";
  json += "}";
  return json;
}

std::string render_index_json(const ParseSummary &summary, Strategy strategy, std::size_t chunk_size,
                              int threads, MaskAlgorithm mask_algorithm) {
  std::string json = "{\n";
  json +=
      "  \"summary\": " + render_summary_json(summary, strategy, chunk_size, threads, mask_algorithm) +
      ",\n";
  json += "  \"structural_indexes\": [";
  for (std::size_t i = 0; i < summary.structural_indexes.size(); ++i) {
    if (i != 0) {
      json += ", ";
    }
    json += std::to_string(summary.structural_indexes[i]);
  }
  json += "]\n";
  json += "}";
  return json;
}

void print_usage(const char *program_name) {
  std::cerr << "Usage:\n"
            << "  " << program_name
            << " --input <file> [--output <file>] [--mode mask|mask-benchmark|summary|index]\n"
            << "               [--strategy seq|omp] [--mask-algorithm algo1|algo2|algo3]\n"
            << "               [--chunk-size N] [--threads N]\n"
            << "  " << program_name << " <input.json> [output.txt] [chunk_size] [num_threads]\n\n"
            << "Modes:\n"
            << "  mask    prints the 0/1 string mask\n"
            << "  mask-benchmark builds only the string mask and prints minimal metadata\n"
            << "  summary prints JSON summary counts useful for analysis\n"
            << "  index   prints summary plus structural character indexes\n\n"
            << "Mask algorithms:\n"
            << "  seq strategy always uses the sequential mask builder\n"
            << "  algo1   original chunk simulation OpenMP mask builder\n"
            << "  algo2   optimized chunk-summary OpenMP mask builder\n"
            << "  algo3   quote-inclusive OpenMP scan-based mask builder\n";
}

OutputMode parse_mode(const std::string &value) {
  if (value == "mask") {
    return OutputMode::Mask;
  }
  if (value == "mask-benchmark") {
    return OutputMode::MaskBenchmark;
  }
  if (value == "summary") {
    return OutputMode::Summary;
  }
  if (value == "index") {
    return OutputMode::Index;
  }
  throw std::runtime_error("Unsupported mode: " + value);
}

Strategy parse_strategy(const std::string &value) {
  if (value == "seq") {
    return Strategy::Sequential;
  }
  if (value == "omp") {
    return Strategy::OpenMP;
  }
  throw std::runtime_error("Unsupported strategy: " + value);
}

MaskAlgorithm parse_mask_algorithm(const std::string &value) {
  if (value == "algo1") {
    return MaskAlgorithm::Algo1;
  }
  if (value == "algo2") {
    return MaskAlgorithm::Algo2;
  }
  if (value == "algo3") {
    return MaskAlgorithm::Algo3;
  }
  throw std::runtime_error("Unsupported mask algorithm: " + value);
}

Config parse_flag_arguments(int argc, char **argv) {
  Config config;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    auto require_value = [&](const std::string &flag) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + flag);
      }
      return argv[++i];
    };

    if (arg == "--input") {
      config.input_path = require_value(arg);
    } else if (arg == "--output") {
      config.output_path = require_value(arg);
    } else if (arg == "--chunk-size") {
      config.chunk_size = std::stoull(require_value(arg));
    } else if (arg == "--threads") {
      config.num_threads = std::stoi(require_value(arg));
    } else if (arg == "--mode") {
      config.mode = parse_mode(require_value(arg));
    } else if (arg == "--strategy") {
      config.strategy = parse_strategy(require_value(arg));
    } else if (arg == "--mask-algorithm") {
      config.mask_algorithm = parse_mask_algorithm(require_value(arg));
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  if (config.input_path.empty()) {
    throw std::runtime_error("--input is required.");
  }

  return config;
}

Config parse_legacy_arguments(int argc, char **argv) {
  if (argc < 2 || argc > 5) {
    print_usage(argv[0]);
    std::exit(1);
  }

  Config config;
  config.input_path = argv[1];
  config.output_path = argc >= 3 ? argv[2] : "";
  config.chunk_size = argc >= 4 ? std::stoull(argv[3]) : (1 << 14);
  config.num_threads = argc >= 5 ? std::stoi(argv[4]) : 0;
  config.mode = OutputMode::Mask;
  config.strategy = config.num_threads == 1 ? Strategy::Sequential : Strategy::OpenMP;
  return config;
}

Config parse_arguments(int argc, char **argv) {
  if (argc >= 2 && std::string(argv[1]).rfind("--", 0) == 0) {
    return parse_flag_arguments(argc, argv);
  }
  return parse_legacy_arguments(argc, argv);
}

}  // namespace parjson
