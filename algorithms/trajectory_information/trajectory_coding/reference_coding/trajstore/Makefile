CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pedantic

TARGET = bin/trajstore
OBJDIR = obj
SRCDIR = src

OBJS = $(OBJDIR)/main.o

default: $(TARGET)

$(TARGET): $(OBJS)
	@if not exist bin mkdir bin
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

$(OBJDIR)/main.o: $(SRCDIR)/main.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	@if not exist $(OBJDIR) mkdir $(OBJDIR)

clean:
	-if exist $(OBJDIR) rmdir /s /q $(OBJDIR)

