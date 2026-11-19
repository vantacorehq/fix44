CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

.PHONY: test clean

test: build/test
	./build/test

build/test: tests/test.cpp src/fix44.cpp include/fix44.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -o $@ tests/test.cpp src/fix44.cpp

clean:
	rm -rf build
