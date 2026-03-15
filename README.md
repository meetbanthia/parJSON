# parJSON
Parallel JSON Parser using OpenMP

I need to parse a json file using parallel openmp stratgies. Speciffically i want to calcuate at what postions are string representing by bit 1 and other positions with 0. This final array of bits should be the output.

## Implementation

This project now includes a C++17 OpenMP CLI that reads a JSON file and outputs a bit mask of the same length:

- `1` means the character is inside a JSON string
- `0` means the character is outside a JSON string
- quote characters themselves are marked as `0`
- escaped quotes like `\"` remain inside the string and are marked as `1`

The parser uses chunked processing so each chunk can be simulated in parallel while still handling chunk-boundary cases like escaped quotes and strings that span across chunks.

## Build

```bash
make
```

## Run

```bash
./parjson path/to/input.json
./parjson path/to/input.json output.txt
./parjson path/to/input.json output.txt 65536
```

You can also try the included sample:

```bash
make run
```
