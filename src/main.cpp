#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

enum class Strategy {
  Sequential,
  OpenMP,
};

enum class OutputMode {
  Mask,
  Summary,
  Index,
};

struct ChunkState {
  bool in_string = false;
  bool odd_backslash_run = false;
};

struct ChunkSimulation {
  ChunkState outgoing[2][2];
  std::vector<unsigned char> masks[2][2];
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
};

struct Config {
  std::string input_path;
  std::string output_path;
  std::size_t chunk_size = 1 << 14;
  int num_threads = 0;
  Strategy strategy = Strategy::OpenMP;
  OutputMode mode = OutputMode::Summary;
};

bool is_whitespace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

bool is_number_char(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' ||
         c == 'e' || c == 'E';
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

std::vector<unsigned char> build_mask_parallel(const std::string &data, std::size_t chunk_size) {
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

std::vector<unsigned char> build_mask_sequential(const std::string &data) {
  std::vector<unsigned char> mask(data.size(), 0);
  bool in_string = false;
  bool odd_backslash_run = false;

  for (std::size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];

    if (c == '\\') {
      mask[i] = static_cast<unsigned char>(1);

      //toggle odd backslash run
      odd_backslash_run = !odd_backslash_run;
      continue;
    }

    const bool escaped_quote = (c == '"') && odd_backslash_run;
    odd_backslash_run = false;

    if (c == '"' && !escaped_quote) {
      in_string = !in_string;
      mask[i] = 0;
      continue;
    }

    mask[i] = static_cast<unsigned char>(in_string ? 1 : 0);
  }

  return mask;
}

ParseSummary analyze_document(const std::string &data, const std::vector<unsigned char> &mask,
                              bool include_indexes) {
  ParseSummary summary;
  summary.bytes = data.size();

  int depth = 0;
  std::size_t quote_delimiters = 0;

  for (std::size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];

    if (mask[i] == 1) {
      ++summary.string_characters;
      continue;
    }

    if (c == '"') {
      ++quote_delimiters;
      continue;
    }

    if (is_whitespace(c)) {
      continue;
    }

    switch (c) {
      case '{':
        ++summary.object_opens;
        ++summary.structural_characters;
        if (include_indexes) {
          summary.structural_indexes.push_back(i);
        }
        ++depth;
        summary.max_depth = std::max(summary.max_depth, static_cast<std::size_t>(depth));
        continue;
      case '[':
        ++summary.array_opens;
        ++summary.structural_characters;
        if (include_indexes) {
          summary.structural_indexes.push_back(i);
        }
        ++depth;
        summary.max_depth = std::max(summary.max_depth, static_cast<std::size_t>(depth));
        continue;
      case '}':
      case ']':
        ++summary.structural_characters;
        if (include_indexes) {
          summary.structural_indexes.push_back(i);
        }
        depth = std::max(0, depth - 1);
        continue;
      case ':':
        ++summary.colon_count;
        ++summary.structural_characters;
        if (include_indexes) {
          summary.structural_indexes.push_back(i);
        }
        continue;
      case ',':
        ++summary.comma_count;
        ++summary.structural_characters;
        if (include_indexes) {
          summary.structural_indexes.push_back(i);
        }
        continue;
      default:
        break;
    }

    if (c == 't' && data.compare(i, 4, "true") == 0) {
      ++summary.true_literals;
      i += 3;
      continue;
    }

    if (c == 'f' && data.compare(i, 5, "false") == 0) {
      ++summary.false_literals;
      i += 4;
      continue;
    }

    if (c == 'n' && data.compare(i, 4, "null") == 0) {
      ++summary.null_literals;
      i += 3;
      continue;
    }

    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      ++summary.number_literals;
      while (i + 1 < data.size() && mask[i + 1] == 0 && is_number_char(data[i + 1])) {
        ++i;
      }
      continue;
    }
  }

  summary.string_literals = quote_delimiters / 2;
  return summary;
}

ParseResult parse_json(const std::string &data, const Config &config) {
  ParseResult result;

  if (config.strategy == Strategy::Sequential) {
    result.string_mask = build_mask_sequential(data);
  } else {
#ifdef _OPENMP
    result.string_mask = build_mask_parallel(data, config.chunk_size);
#else
    result.string_mask = build_mask_sequential(data);
#endif
  }

  result.summary = analyze_document(data, result.string_mask, config.mode == OutputMode::Index);
  return result;
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

std::string render_summary_json(const ParseSummary &summary, Strategy strategy, std::size_t chunk_size,
                                int threads) {
  std::string json = "{\n";
  json += "  \"strategy\": \"" +
          std::string(strategy == Strategy::Sequential ? "sequential" : "openmp") + "\",\n";
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
                              int threads) {
  std::string json = "{\n";
  json += "  \"summary\": " + render_summary_json(summary, strategy, chunk_size, threads) + ",\n";
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
            << " --input <file> [--output <file>] [--mode mask|summary|index]\n"
            << "               [--strategy seq|omp] [--chunk-size N] [--threads N]\n"
            << "  " << program_name << " <input.json> [output.txt] [chunk_size] [num_threads]\n\n"
            << "Modes:\n"
            << "  mask    prints the 0/1 string mask\n"
            << "  summary prints JSON summary counts useful for analysis\n"
            << "  index   prints summary plus structural character indexes\n";
}

OutputMode parse_mode(const std::string &value) {
  if (value == "mask") {
    return OutputMode::Mask;
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

}  // namespace

int main(int argc, char **argv) {
  try {
    const Config config = parse_arguments(argc, argv);

    if (config.chunk_size == 0) {
      throw std::runtime_error("Chunk size must be greater than zero.");
    }
    if (config.num_threads < 0) {
      throw std::runtime_error("Number of threads cannot be negative.");
    }

  //set num of threads to max threads(number of logical cores) when num_threads not specified
#ifdef _OPENMP
    if (config.num_threads > 0) {
      omp_set_num_threads(config.num_threads);
    }
    const int active_threads = config.strategy == Strategy::Sequential
                                   ? 1
                                   : (config.num_threads > 0 ? config.num_threads : omp_get_max_threads());
#else
    if (config.num_threads > 0 && config.strategy == Strategy::OpenMP) {
      std::cerr << "Warning: OpenMP was requested, but this build does not include OpenMP support.\n";
    }
    const int active_threads = 1;
#endif

    const std::string data = read_file(config.input_path);
    const ParseResult result = parse_json(data, config);

    std::string output;
    if (config.mode == OutputMode::Mask) {
      output = render_mask(result.string_mask);
    } else if (config.mode == OutputMode::Summary) {
      output = render_summary_json(result.summary, config.strategy, config.chunk_size, active_threads);
      output.push_back('\n');
    } else {
      output = render_index_json(result.summary, config.strategy, config.chunk_size, active_threads);
      output.push_back('\n');
    }

    if (config.output_path.empty()) {
      std::cout << output;
    } else {
      write_text(config.output_path, output);
    }

    std::cerr << "Processed " << data.size() << " bytes with strategy="
              << (config.strategy == Strategy::Sequential ? "seq" : "omp")
              << " threads=" << active_threads << ".\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }

  return 0;
}
