#include "parjson.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <omp.h>

namespace parjson {
namespace {

static inline std::uint8_t pack_state(bool in_string, bool odd_backslash_run) {
  return static_cast<std::uint8_t>((static_cast<std::uint8_t>(in_string) << 1) |
                                   static_cast<std::uint8_t>(odd_backslash_run));
}

static inline bool state_in_string(std::uint8_t state) {
  return ((state >> 1) & 1) != 0;
}

static inline bool state_odd_backslash_run(std::uint8_t state) {
  return (state & 1) != 0;
}

bool is_whitespace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

bool is_number_char(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' ||
         c == 'e' || c == 'E';
}

struct ChunkAnalysis {
  ParseSummary summary;
  std::int64_t depth_delta = 0;
  std::int64_t min_prefix_depth = 0;
  std::size_t quote_delimiters = 0;
  std::size_t quote_inclusive_string_literals = 0;
};

using ChunkTransitions = std::array<std::uint8_t, 4>;
using ChunkMasks = std::array<std::vector<unsigned char>, 4>;

void simulate_chunk(const std::string &data, std::size_t begin, std::size_t end,
                    ChunkTransitions &outgoing, ChunkMasks &masks) {
  const std::size_t chunk_size = end - begin;

  for (std::uint8_t start_state = 0; start_state < 4; ++start_state) {
    auto &mask = masks[start_state];
    mask.assign(chunk_size, 0);

    bool in_string = state_in_string(start_state);
    bool odd_backslash_run = state_odd_backslash_run(start_state);

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

    outgoing[start_state] = pack_state(in_string, odd_backslash_run);
  }
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
  std::vector<ChunkTransitions> chunk_transitions(chunk_count);
  std::vector<ChunkMasks> chunk_masks(chunk_count);
  std::vector<std::uint8_t> incoming_states(chunk_count);

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t begin = static_cast<std::size_t>(chunk_index) * chunk_size;
    const std::size_t end = std::min(begin + chunk_size, length);
    simulate_chunk(data, begin, end, chunk_transitions[static_cast<std::size_t>(chunk_index)],
                   chunk_masks[static_cast<std::size_t>(chunk_index)]);
  }

  std::uint8_t state = pack_state(false, false);
  for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
    incoming_states[chunk_index] = state;
    state = chunk_transitions[chunk_index][state];
  }

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t current = static_cast<std::size_t>(chunk_index);
    const std::size_t begin = current * chunk_size;
    const auto &chunk_mask = chunk_masks[current][incoming_states[current]];

    for (std::size_t offset = 0; offset < chunk_mask.size(); ++offset) {
      mask[begin + offset] = chunk_mask[offset];
    }
  }

  return mask;
}

static inline std::uint8_t simulate_chunk_end_state(const char *data, std::size_t begin,
                                                    std::size_t end, std::uint8_t start_state) {
  bool in_string = state_in_string(start_state);
  bool odd_backslash_run = state_odd_backslash_run(start_state);

  for (std::size_t i = begin; i < end; ++i) {
    const char c = data[i];

    if (c == '\\') {
      odd_backslash_run = !odd_backslash_run;
    } else {
      if (c == '"' && !odd_backslash_run) {
        in_string = !in_string;
      }
      odd_backslash_run = false;
    }
  }

  return pack_state(in_string, odd_backslash_run);
}

static inline ChunkTransitions summarize_chunk(const char *data, std::size_t begin,
                                               std::size_t end) {
  ChunkTransitions summary{};
  for (std::uint8_t s = 0; s < 4; ++s) {
    summary[s] = simulate_chunk_end_state(data, begin, end, s);
  }
  return summary;
}

static inline void render_chunk_mask(const char *data, std::size_t begin, std::size_t end,
                                     std::uint8_t start_state, unsigned char *out) {
  bool in_string = state_in_string(start_state);
  bool odd_backslash_run = state_odd_backslash_run(start_state);

  for (std::size_t i = begin; i < end; ++i) {
    const char c = data[i];

    if (c == '"') {
      const bool unescaped = !odd_backslash_run;
      out[i] = 1;

      if (unescaped) {
        in_string = !in_string;
      }

      odd_backslash_run = false;
    } else {
      out[i] = static_cast<unsigned char>(in_string);

      if (c == '\\') {
        odd_backslash_run = !odd_backslash_run;
      } else {
        odd_backslash_run = false;
      }
    }
  }
}

std::vector<unsigned char> build_mask_parallel_optimized(const std::string &data,
                                                         std::size_t chunk_size) {
  if (chunk_size == 0) {
    throw std::runtime_error("Chunk size must be greater than zero.");
  }

  const std::size_t length = data.size();
  std::vector<unsigned char> mask(length, 0);
  if (length == 0) {
    return mask;
  }

  const char *input = data.data();
  unsigned char *output = mask.data();

  const std::size_t chunk_count = (length + chunk_size - 1) / chunk_size;

  std::vector<ChunkTransitions> summaries(chunk_count);
  std::vector<std::uint8_t> incoming_states(chunk_count);

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t begin = static_cast<std::size_t>(chunk_index) * chunk_size;
    const std::size_t end = std::min(begin + chunk_size, length);
    summaries[static_cast<std::size_t>(chunk_index)] = summarize_chunk(input, begin, end);
  }

  std::uint8_t state = pack_state(false, false);
  for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
    incoming_states[chunk_index] = state;
    state = summaries[chunk_index][state];
  }

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t current = static_cast<std::size_t>(chunk_index);
    const std::size_t begin = current * chunk_size;
    const std::size_t end = std::min(begin + chunk_size, length);
    render_chunk_mask(input, begin, end, incoming_states[current], output);
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

std::vector<unsigned char> build_mask_quote_inclusive_openmp(const std::string &data) {
  const std::size_t length = data.size();
  std::vector<unsigned char> mask(length, 0);
  if (length == 0) {
    return mask;
  }

  std::vector<unsigned char> backslash_run_odd(length + 1, 0);
  int max_run_start = 0;
#pragma omp parallel for reduction(inscan, max : max_run_start)
  for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(length); ++i) {
    max_run_start = std::max(
        max_run_start,
        data[static_cast<std::size_t>(i)] == '\\'
            ? ((i == 0 || data[static_cast<std::size_t>(i) - 1] != '\\') ? static_cast<int>(i) : -1)
            : 0);
#pragma omp scan inclusive(max_run_start)
    backslash_run_odd[static_cast<std::size_t>(i) + 1] = static_cast<unsigned char>(
        data[static_cast<std::size_t>(i)] == '\\' ? ((i + 1 - max_run_start) & 1) : 0);
  }

  int quote_count = 0;
#pragma omp parallel for reduction(inscan, + : quote_count)
  for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(length); ++i) {
    quote_count +=
        (data[static_cast<std::size_t>(i)] == '"' &&
         !(i > 0 && data[static_cast<std::size_t>(i) - 1] == '\\' &&
           backslash_run_odd[static_cast<std::size_t>(i)]))
            ? 1
            : 0;
#pragma omp scan inclusive(quote_count)
    mask[static_cast<std::size_t>(i)] = static_cast<unsigned char>(
        ((quote_count & 1) != 0) || data[static_cast<std::size_t>(i)] == '"');
  }

  return mask;
}

ParseSummary analyze_document(const std::string &data, const std::vector<unsigned char> &mask,
                              bool include_indexes) {
  ParseSummary summary;
  summary.bytes = data.size();

  int depth = 0;
  std::size_t quote_delimiters = 0;
  std::size_t quote_inclusive_string_literals = 0;

  for (std::size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];

    if (mask[i] == 1) {
      if (c == '"') {
        if (i == 0 || mask[i - 1] == 0) {
          ++quote_inclusive_string_literals;
        }
      } else {
        ++summary.string_characters;
      }
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

  summary.string_literals =
      quote_inclusive_string_literals != 0 ? quote_inclusive_string_literals : quote_delimiters / 2;
  return summary;
}

static inline bool starts_number_literal(const std::string &data,
                                         const std::vector<unsigned char> &mask,
                                         std::size_t index) {
  const char c = data[index];
  if (!(c == '-' || std::isdigit(static_cast<unsigned char>(c)))) {
    return false;
  }

  if (index == 0 || mask[index - 1] != 0) {
    return true;
  }

  return !is_number_char(data[index - 1]);
}

ChunkAnalysis analyze_chunk(const std::string &data, const std::vector<unsigned char> &mask,
                            std::size_t begin, std::size_t end, bool include_indexes) {
  ChunkAnalysis chunk;
  std::int64_t depth = 0;

  for (std::size_t i = begin; i < end; ++i) {
    const char c = data[i];

    if (mask[i] == 1) {
      if (c == '"') {
        if (i == 0 || mask[i - 1] == 0) {
          ++chunk.quote_inclusive_string_literals;
        }
      } else {
        ++chunk.summary.string_characters;
      }
      continue;
    }

    if (c == '"') {
      ++chunk.quote_delimiters;
      continue;
    }

    if (is_whitespace(c)) {
      continue;
    }

    switch (c) {
      case '{':
        ++chunk.summary.object_opens;
        ++chunk.summary.structural_characters;
        if (include_indexes) {
          chunk.summary.structural_indexes.push_back(i);
        }
        ++depth;
        continue;
      case '[':
        ++chunk.summary.array_opens;
        ++chunk.summary.structural_characters;
        if (include_indexes) {
          chunk.summary.structural_indexes.push_back(i);
        }
        ++depth;
        continue;
      case '}':
      case ']':
        ++chunk.summary.structural_characters;
        if (include_indexes) {
          chunk.summary.structural_indexes.push_back(i);
        }
        --depth;
        chunk.min_prefix_depth = std::min(chunk.min_prefix_depth, depth);
        continue;
      case ':':
        ++chunk.summary.colon_count;
        ++chunk.summary.structural_characters;
        if (include_indexes) {
          chunk.summary.structural_indexes.push_back(i);
        }
        continue;
      case ',':
        ++chunk.summary.comma_count;
        ++chunk.summary.structural_characters;
        if (include_indexes) {
          chunk.summary.structural_indexes.push_back(i);
        }
        continue;
      default:
        break;
    }

    if (c == 't' && data.compare(i, 4, "true") == 0) {
      ++chunk.summary.true_literals;
      i += 3;
      continue;
    }

    if (c == 'f' && data.compare(i, 5, "false") == 0) {
      ++chunk.summary.false_literals;
      i += 4;
      continue;
    }

    if (c == 'n' && data.compare(i, 4, "null") == 0) {
      ++chunk.summary.null_literals;
      i += 3;
      continue;
    }

    if (starts_number_literal(data, mask, i)) {
      ++chunk.summary.number_literals;
      while (i + 1 < end && mask[i + 1] == 0 && is_number_char(data[i + 1])) {
        ++i;
      }
    }
  }

  chunk.depth_delta = depth;
  return chunk;
}

std::size_t compute_chunk_max_depth(const std::string &data, const std::vector<unsigned char> &mask,
                                    std::size_t begin, std::size_t end,
                                    std::int64_t starting_depth) {
  std::int64_t depth = starting_depth;
  std::size_t max_depth = static_cast<std::size_t>(std::max<std::int64_t>(0, depth));

  for (std::size_t i = begin; i < end; ++i) {
    if (mask[i] == 1) {
      continue;
    }

    switch (data[i]) {
      case '{':
      case '[':
        ++depth;
        max_depth = std::max(max_depth, static_cast<std::size_t>(depth));
        break;
      case '}':
      case ']':
        depth = std::max<std::int64_t>(0, depth - 1);
        break;
      default:
        break;
    }
  }

  return max_depth;
}

ParseSummary analyze_document_openmp(const std::string &data, const std::vector<unsigned char> &mask,
                                     bool include_indexes, std::size_t chunk_size) {
  if (chunk_size == 0) {
    throw std::runtime_error("Chunk size must be greater than zero.");
  }

  ParseSummary summary;
  summary.bytes = data.size();
  if (data.empty()) {
    return summary;
  }

  const std::size_t chunk_count = (data.size() + chunk_size - 1) / chunk_size;
  std::vector<ChunkAnalysis> chunk_analyses(chunk_count);
  std::vector<std::int64_t> starting_depths(chunk_count, 0);
  std::vector<std::size_t> chunk_max_depths(chunk_count, 0);

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t begin = static_cast<std::size_t>(chunk_index) * chunk_size;
    const std::size_t end = std::min(begin + chunk_size, data.size());
    chunk_analyses[static_cast<std::size_t>(chunk_index)] =
        analyze_chunk(data, mask, begin, end, include_indexes);
  }

  std::int64_t current_depth = 0;
  for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
    starting_depths[chunk_index] = current_depth;
    const auto &chunk = chunk_analyses[chunk_index];
    current_depth = current_depth + chunk.depth_delta -
                    std::min<std::int64_t>(0, current_depth + chunk.min_prefix_depth);
  }

#pragma omp parallel for schedule(static) if (chunk_count > 1)
  for (std::ptrdiff_t chunk_index = 0; chunk_index < static_cast<std::ptrdiff_t>(chunk_count);
       ++chunk_index) {
    const std::size_t current = static_cast<std::size_t>(chunk_index);
    const std::size_t begin = current * chunk_size;
    const std::size_t end = std::min(begin + chunk_size, data.size());
    chunk_max_depths[current] =
        compute_chunk_max_depth(data, mask, begin, end, starting_depths[current]);
  }

  std::size_t total_indexes = 0;
  std::size_t quote_delimiters = 0;
  std::size_t quote_inclusive_string_literals = 0;

  for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
    auto &chunk = chunk_analyses[chunk_index];
    summary.string_characters += chunk.summary.string_characters;
    summary.number_literals += chunk.summary.number_literals;
    summary.true_literals += chunk.summary.true_literals;
    summary.false_literals += chunk.summary.false_literals;
    summary.null_literals += chunk.summary.null_literals;
    summary.object_opens += chunk.summary.object_opens;
    summary.array_opens += chunk.summary.array_opens;
    summary.structural_characters += chunk.summary.structural_characters;
    summary.colon_count += chunk.summary.colon_count;
    summary.comma_count += chunk.summary.comma_count;
    summary.max_depth = std::max(summary.max_depth, chunk_max_depths[chunk_index]);
    quote_delimiters += chunk.quote_delimiters;
    quote_inclusive_string_literals += chunk.quote_inclusive_string_literals;
    total_indexes += chunk.summary.structural_indexes.size();
  }

  if (include_indexes) {
    summary.structural_indexes.reserve(total_indexes);
    for (auto &chunk : chunk_analyses) {
      summary.structural_indexes.insert(summary.structural_indexes.end(),
                                        chunk.summary.structural_indexes.begin(),
                                        chunk.summary.structural_indexes.end());
    }
  }

  summary.string_literals =
      quote_inclusive_string_literals != 0 ? quote_inclusive_string_literals : quote_delimiters / 2;
  return summary;
}

}  // namespace

ParseResult parse_json(const std::string &data, const Config &config) {
  ParseResult result;
  const auto mask_start = std::chrono::steady_clock::now();

  if (config.strategy == Strategy::Sequential) {
    result.string_mask = build_mask_sequential(data);
  } else {
    switch (config.mask_algorithm) {
      case MaskAlgorithm::Algo1:
        result.string_mask = build_mask_parallel(data, config.chunk_size);
        break;
      case MaskAlgorithm::Algo2:
        result.string_mask = build_mask_parallel_optimized(data, config.chunk_size);
        break;
      case MaskAlgorithm::Algo3:
        result.string_mask = build_mask_quote_inclusive_openmp(data);
        break;
    }
  }

  const auto mask_end = std::chrono::steady_clock::now();
  result.mask_build_seconds = std::chrono::duration<double>(mask_end - mask_start).count();

  if (config.mode != OutputMode::MaskBenchmark) {
    if (config.strategy == Strategy::Sequential) {
      result.summary = analyze_document(data, result.string_mask, config.mode == OutputMode::Index);
    } else {
      result.summary = analyze_document_openmp(data, result.string_mask,
                                               config.mode == OutputMode::Index, config.chunk_size);
    }
  }

  return result;
}

}  // namespace parjson
