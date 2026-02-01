# Makefile for sqlite3db library
# This provides an alternative to CMake for simple builds

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude
LIBS = -lsqlite3

# Source files
LIB_SOURCES = \
	src/connection.cpp \
	src/statement.cpp \
	src/transaction.cpp \
	src/migration.cpp \
	src/repository.cpp

# Object files
LIB_OBJECTS = $(LIB_SOURCES:.cpp=.o)

# Targets
LIBRARY = libsqlite3db.a
EXAMPLE = sqlite3db_example
TESTS = sqlite3db_tests

.PHONY: all clean example tests install

all: $(LIBRARY) example

# Build static library
$(LIBRARY): $(LIB_OBJECTS)
	ar rcs $@ $^
	@echo "Built static library: $@"

# Build object files
src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build example
example: $(LIBRARY)
	$(CXX) $(CXXFLAGS) $(INCLUDES) examples/main.cpp -L. -lsqlite3db $(LIBS) -o $(EXAMPLE)
	@echo "Built example: $(EXAMPLE)"

# Build tests
tests: $(LIBRARY)
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_main.cpp -L. -lsqlite3db $(LIBS) -o $(TESTS)
	@echo "Built tests: $(TESTS)"

# Run tests
run-tests: tests
	./$(TESTS)

# Run example
run-example: example
	./$(EXAMPLE)

# Clean build artifacts
clean:
	rm -f $(LIB_OBJECTS) $(LIBRARY) $(EXAMPLE) $(TESTS)

# Installation (adjust PREFIX as needed)
PREFIX ?= /usr/local
install: $(LIBRARY)
	install -d $(PREFIX)/lib
	install -d $(PREFIX)/include/sqlite3db
	install -m 644 $(LIBRARY) $(PREFIX)/lib/
	install -m 644 include/sqlite3db/*.hpp $(PREFIX)/include/sqlite3db/

# Help
help:
	@echo "Available targets:"
	@echo "  all         - Build library and example (default)"
	@echo "  example     - Build the example program"
	@echo "  tests       - Build the test program"
	@echo "  run-example - Build and run the example"
	@echo "  run-tests   - Build and run tests"
	@echo "  clean       - Remove build artifacts"
	@echo "  install     - Install library and headers"
	@echo ""
	@echo "Prerequisites:"
	@echo "  sudo apt install libsqlite3-dev  (Ubuntu/Debian)"
	@echo "  brew install sqlite3             (macOS)"
