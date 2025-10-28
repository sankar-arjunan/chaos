# ====== Configuration ======
PYTHON      := python3
PYBIND11_INC:= $(shell $(PYTHON) -m pybind11 --includes)
PY_EXT      := $(shell $(PYTHON)-config --extension-suffix)
PY_LIBDIR   := $(shell $(PYTHON)-config --configdir)
CXX         := c++
CXXFLAGS    := -O3 -std=c++17 -fPIC
LDFLAGS     := -llz4 -L$(PY_LIBDIR) -lpython3.11
SRC_COMMON  := encoder_parallel.cpp datastruct.cpp decoder.cpp decoder_parallel.cpp simdjson.cpp encoder.cpp

# ====== Targets ======
all: pychaos cmdline

pychaos: pychaos_query.cpp $(SRC_COMMON)
	$(CXX) $(CXXFLAGS) -shared $(PYBIND11_INC) \
	$^ -o pychaos$(PY_EXT) -I. $(LDFLAGS)

cmdline: main.cpp $(SRC_COMMON)
	$(CXX) -std=c++17 -O3 $^ -o $@ -I. -llz4

clean:
	rm -f pychaos*.so cmdline
	rm -rf __pycache__


run-bin:
	./cmdline

.PHONY: all clean run-py run-bin
