# parJSON

`parJSON` is a C++17 JSON stage-1 style parser project that uses OpenMP to parallelize scanning. The core idea is to identify JSON string regions in parallel, then use that mask to build useful parser outputs such as:

- a bit mask with `1` for characters inside strings and `0` elsewhere
- a structural character index for `{ } [ ] : ,` outside strings
- a summary of JSON contents such as object, array, string, number, boolean, and null counts

This gives you both a sequential baseline and an OpenMP version that can be benchmarked for speedup across thread counts, input sizes, and JSON workload types.

## Parallel strategy

The implementation is split into two stages:

1. Each chunk is simulated independently for all possible incoming boundary states.
2. A short prefix pass determines the real boundary state of every chunk, and then chunks materialize their final string masks in parallel.

This lets the parser handle tricky cases such as:

- strings that span chunk boundaries
- escaped quotes like `\"`
- long backslash runs split across chunks

After the string mask is known, the parser performs a structural scan to compute summary statistics or structural indexes. The summary mode is a useful application output for experiments because it shows the parser is doing meaningful work beyond printing raw bits.

## Build

```bash
make
```

On macOS, the `Makefile` will use Homebrew `libomp` if it is installed. On Linux, it uses the usual `-fopenmp` flags.

## Run

Legacy mask mode:

```bash
./parjson sample/sample.json
./parjson sample/sample.json output.txt 65536 4
```

Modern flag-based interface:

```bash
./parjson --input sample/sample.json --mode summary --strategy seq
./parjson --input sample/sample.json --mode summary --strategy omp --threads 4
./parjson --input sample/sample.json --mode index --strategy omp --threads 8 --chunk-size 65536
./parjson --input sample/sample.json --mode mask --output output.txt
```

Supported modes:

- `mask`: prints the `0/1` string mask
- `summary`: prints a JSON summary of the parsed document
- `index`: prints the summary plus all structural character positions outside strings

Supported strategies:

- `seq`: sequential baseline
- `omp`: OpenMP chunked parser

## Example summary output

```json
{
  "strategy": "openmp",
  "threads": 4,
  "chunk_size": 16384,
  "bytes": 72,
  "string_characters": 48,
  "string_literals": 4,
  "number_literals": 3,
  "true_literals": 0,
  "false_literals": 0,
  "null_literals": 0,
  "object_opens": 1,
  "array_opens": 1,
  "colon_count": 3,
  "comma_count": 3,
  "structural_characters": 8,
  "max_depth": 2
}
```

## Validation

You can verify the parser summary against Python’s built-in `json` module:

```bash
make verify
```

This checks both the sequential and OpenMP implementations on the sample input.

## Experiments

The repo includes a reproducible benchmarking pipeline for evaluating:

- sequential vs OpenMP runtime
- speedup as thread count increases
- scaling with input size
- sensitivity to JSON workload type

### 1. Generate a benchmark input

```bash
make generate-benchmark-input
```

Or generate a custom workload:

```bash
python3 scripts/generate_benchmark_json.py benchmark/generated.json --objects 50000 --text-length 512 --profile nested
```

Available profiles:

- `mixed`
- `flat`
- `nested`
- `string-heavy`
- `numeric-heavy`

### 2. Run thread-scaling experiments

```bash
make benchmark
```

This writes `results/benchmark.csv` with:

- `label`
- `input_bytes`
- `strategy`
- `threads`
- `runs`
- `mean_seconds`
- `stdev_seconds`
- `throughput_mb_s`
- `speedup_vs_seq`
- `speedup_vs_omp_1`

### 3. Run size and workload experiments

```bash
make benchmark-size
```

For a larger sweep:

```bash
make benchmark-size-big
```

This writes `results/size_thread_matrix.csv` with:

- `profile`
- `objects`
- `num_chars`
- `file_size_bytes`
- `strategy`
- `threads`
- `runs`
- `mean_seconds`
- `stdev_seconds`
- `throughput_mb_s`
- `speedup_vs_seq`

### 4. Plot graphs

```bash
make graphs
```

This generates:

- `results/plots/thread_scaling.svg`
- `results/plots/size_scaling_by_profile.svg`
- `results/plots/throughput_by_profile.svg`
