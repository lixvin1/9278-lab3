CXX ?= g++
CXXFLAGS ?= -Wall -Wextra -g -std=c++11
TARGET ?= myshell
SRCS = command.cc tokenizer.cc

.PHONY: all build run clean

all: build

build:
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

run: build
	./$(TARGET)

clean:
	rm -f $(TARGET) shell.log
