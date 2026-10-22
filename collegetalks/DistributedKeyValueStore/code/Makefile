CXX = clang++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -Iinclude

TARGET = kv_bench
SRC = src/main.cpp

all: $(TARGET)

$(TARGET): $(SRC) include/*.hpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
