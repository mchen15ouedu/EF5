# Simple Makefile to test lake CSV reading functionality
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra

all: test_lake_csv_reading

test_lake_csv_reading: test_lake_csv_reading.cpp
	$(CXX) $(CXXFLAGS) -o test_lake_csv_reading test_lake_csv_reading.cpp

test: test_lake_csv_reading
	./test_lake_csv_reading

clean:
	rm -f test_lake_csv_reading

.PHONY: all test clean 