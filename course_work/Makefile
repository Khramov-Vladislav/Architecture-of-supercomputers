CXX = g++
MPICXX ?= mpic++
CLANG_FORMAT ?= clang-format
PYTHON ?= python3
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic
OMPFLAGS ?= -fopenmp
FORMAT_STYLE := {BasedOnStyle: LLVM, IndentWidth: 4}

SOURCE := src/matmul.cpp
TARGETS := build/matmul-seq build/matmul-omp build/matmul-mpi build/matmul-hybrid

.PHONY: all lint report
all: $(TARGETS)

build:
	mkdir -p $@

build/matmul-seq: $(SOURCE) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

build/matmul-omp: $(SOURCE) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OMPFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

build/matmul-mpi: $(SOURCE) | build
	$(MPICXX) $(CPPFLAGS) $(CXXFLAGS) -DOMPI_SKIP_MPICXX -DUSE_MPI $< $(LDFLAGS) $(LDLIBS) -o $@

build/matmul-hybrid: $(SOURCE) | build
	$(MPICXX) $(CPPFLAGS) $(CXXFLAGS) $(OMPFLAGS) -DOMPI_SKIP_MPICXX -DUSE_MPI $< $(LDFLAGS) $(LDLIBS) -o $@

lint:
	$(CLANG_FORMAT) --dry-run --Werror --style='$(FORMAT_STYLE)' $(SOURCE)

report:
	$(PYTHON) bench/summarize.py
	cd report && latexmk -xelatex -outdir=build -file-line-error -halt-on-error -interaction=nonstopmode main.tex
	cp report/build/main.pdf report/main.pdf
