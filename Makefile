RACK_DIR ?= ../..

FLAGS += -Isrc -Isrc/engine
CFLAGS +=
CXXFLAGS += -Wall -Wextra

SOURCES += $(wildcard src/*.cpp) $(wildcard src/engine/*.cpp) $(wildcard src/modules/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

# Unit tests: the engine is plain C++17 and builds without Rack.
TEST_SOURCES := $(wildcard tests/*.cpp) $(wildcard src/engine/*.cpp)
build/tests/run_tests: $(TEST_SOURCES) $(wildcard src/engine/*.hpp) $(wildcard tests/*.h)
	mkdir -p build/tests
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -Isrc/engine -Itests $(TEST_SOURCES) -o $@

test: build/tests/run_tests
	./build/tests/run_tests

.PHONY: test
