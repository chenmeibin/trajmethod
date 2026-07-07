# GraCT

This repository contains the implementation of GraCT introduced in "GraCT: A Grammar-based Compressed Index for Trajectory Data"

# Compiling the GraCT

```
git clone https://gitlab.lbd.org.es/adriangbrandon/gract.git
cd gract
git submodule update --init --recursive
mkdir build
cd build
cmake ..
make
```
# Building GraCT

```
GraCT_create_index ships.txt 120 gract.idx
```

# Running queries

```
GraCT_run_queries gract.idx queries.txt
```


# Authors

* Nieves R. Brisaboa
* Adrián Gómez Brandón
* Gonzalo Navarro
* José R. Paramá