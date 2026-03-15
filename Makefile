CXX := clang++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -pedantic
LDFLAGS :=
TARGET := parjson
SRC := src/main.cpp
UNAME_S := $(shell uname -s)
OMP_PREFIX := $(shell brew --prefix libomp 2>/dev/null)

ifeq ($(UNAME_S),Darwin)
ifneq ($(OMP_PREFIX),)
CXXFLAGS += -Xpreprocessor -fopenmp -I$(OMP_PREFIX)/include
LDFLAGS += -L$(OMP_PREFIX)/lib -lomp
endif
else
CXXFLAGS += -fopenmp
LDFLAGS += -fopenmp
endif

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) sample/sample.json

clean:
	rm -f $(TARGET)
