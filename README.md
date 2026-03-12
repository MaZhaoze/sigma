
<div align="center">
  <img height="180px" src="logo.png" />

  <h1 align="center">sigma-chess</h1>
  <h3>A UCI chess engine written in C++.</h3>

  ![GitHub Repo stars](https://img.shields.io/github/stars/MaZhaoze/sigma?style=flat-square)
  ![GitHub issues](https://img.shields.io/github/issues/MaZhaoze/sigma?style=flat-square)
  ![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue?style=flat-square)
  ![Protocol](https://img.shields.io/badge/Protocol-UCI-green?style=flat-square)
  ![Eval](https://img.shields.io/badge/Eval-HCE%20%2F%20NNUE-purple?style=flat-square)
</div>

---

## Introduction

**sigma-chess** is an experimental chess engine focused on search, evaluation, and engine architecture.

It currently includes:

- UCI protocol support
- Classical evaluation (HCE)
- Experimental NNUE integration
- Transposition table
- Iterative deepening search
- Alpha-beta based search

The project is still under active development.

## Build

`sigma-chess` is built with **g++** and **C++20**.

The default build uses aggressive optimization flags, including:

- `-Ofast`
- `-flto`
- `-march=native`
- `-mtune=native`
- loop and IPA optimizations
- `-fno-exceptions`
- `-fno-rtti`

Build the release version:

```bash
cd src
make
````

This produces:

* `sigma` on Linux
* `sigma.exe` on Windows

Clean and rebuild:

```bash
make rebuild
```

Build a debug version:

```bash
make debug
```

## PGO

The Makefile also includes PGO targets.

Build an instrumented binary:

```bash
make pgo-build
```

Run the engine with some typical commands to collect profile data, then rebuild with profile use:

```bash
make pgo-use
```

## Technical Notes

The engine currently contains:

* Split search code (`Search`, `SearchCore`, `SearchRoot`, `SearchThread`, `SearchTime`, `SearchTT`)
* Classical evaluation
* Experimental NNUE backend
* UCI command handling
* Standard engine options such as `Threads`, `Hash`, `UseNNUE`, and `EvalFile`

NNUE support is currently experimental.
HCE is more stable at the moment.

## UCI Example

```text
uci
isready
ucinewgame
position startpos
go depth 10
```

## Author

**Magnus**
