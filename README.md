# lob: limit order book and matching engine (C++17)

Status: phase 0 of 12 (scaffolding, CI, contract v0.3). No engine code yet. The reference book arrives in phase 1, so CI is intentionally red at this commit.

Specification: [docs/CONTRACTS.md](docs/CONTRACTS.md). Every behaviour is defined there, not in the code.

## Layout

- `docs/CONTRACTS.md`: normative semantics (v0.3).
- `CMakeLists.txt`: build, warning, and sanitizer options.
- `.github/workflows/ci.yml`: GCC release, Clang release, and Clang ASan+UBSan jobs.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOB_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Compilers: GCC 13 or later, Clang 17 or later. Sanitizer build: add `-DLOB_SANITIZE=ON`.

## What is proven

Nothing yet. This commit contains no engine code and no tests.

## What is not proven

- Nothing about matching, modify, or accounting. None of it is implemented.
- No differential test, no randomized invariant test, no fuzzing, no latency measurements.
- No stop orders, no simulator, no feed ingestion, no ring buffer.
- Self-trade prevention is specified as reserved but not implemented.

## Design notes

The naive reference book, planned for phase 1, is kept on purpose as the oracle. Its clarity is the reason it is trusted, so it is not optimized. The fast book will replace the data structures, not the semantics.
