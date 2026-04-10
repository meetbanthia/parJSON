CXX := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -pedantic
LDFLAGS :=
TARGET := parjson
SRC := src/main.cpp src/parjson.cpp src/algo.cpp

CXXFLAGS += -fopenmp
LDFLAGS += -fopenmp

.PHONY: all clean run summary index benchmark benchmark-size benchmark-size-big graphs generate-benchmark-input verify

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) sample/sample.json

summary: $(TARGET)
	./$(TARGET) --input sample/sample.json --mode summary --strategy omp --threads 4

index: $(TARGET)
	./$(TARGET) --input sample/sample.json --mode index --strategy omp --threads 4

benchmark/generated.json:
	python3 scripts/generate_benchmark_json.py benchmark/generated.json --profile mixed

generate-benchmark-input: benchmark/generated.json

benchmark: $(TARGET) benchmark/generated.json
	python3 scripts/run_experiments.py --input benchmark/generated.json --label mixed

benchmark-size: $(TARGET)
	python3 scripts/run_size_experiments.py

benchmark-size-big: $(TARGET)
	python3 scripts/run_size_experiments.py --objects 20000 40000 80000 --threads 1 2 4 8 --text-length 512

graphs: benchmark benchmark-size
	python3 scripts/plot_benchmarks.py

verify: $(TARGET)
	python3 scripts/validate_parser.py --binary ./parjson --input sample/sample.json --strategy seq --threads 1
	python3 scripts/validate_parser.py --binary ./parjson --input sample/sample.json --strategy omp --threads 4

clean:
	rm -f $(TARGET) benchmark/generated.json results/benchmark.csv results/size_thread_matrix.csv
	rm -f /tmp/parjson-mask.txt /tmp/parjson-out.json /tmp/parjson-validate-summary.json
	rm -rf benchmark/size_inputs results/plots
