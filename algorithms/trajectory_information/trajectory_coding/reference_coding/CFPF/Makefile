CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pedantic
SRCDIR = src
TESTDIR = tests
BIN = bin

default: cfpf codec

cfpf:
	@if not exist $(BIN) mkdir $(BIN)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) $(SRCDIR)/cfpf_main.cpp $(SRCDIR)/cfpf_algorithm.cpp $(SRCDIR)/cfpf_preprocessing.cpp -o $(BIN)/cfpf_cli.exe

codec:
	@if not exist $(BIN) mkdir $(BIN)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) $(SRCDIR)/cfpf_codec_main.cpp $(SRCDIR)/huffman_codec.cpp $(SRCDIR)/compact_cfpf_codec.cpp -o $(BIN)/cfpf_codec.exe

evaluate:
	@if not exist $(BIN) mkdir $(BIN)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) $(SRCDIR)/cfpf_evaluate_main.cpp $(SRCDIR)/cfpf_evaluation.cpp -o $(BIN)/cfpf_evaluate.exe

clean:
	-if exist $(BIN) rmdir /s /q $(BIN)
