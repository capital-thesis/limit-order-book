# lob: limit order book and matching engine (C++17)

Status: phase 1 of 12. The naive reference book (the oracle) and its 32 unit tests are committed. The fast book does not exist yet.

Specification: [docs/CONTRACTS.md](docs/CONTRACTS.md). Every behaviour is defined there, not in the code.

## Layout

- `include/lob/types.hpp`: shared types and the event vocabulary.
- `reference/reference_book.hpp`: the naive oracle (`std::map`, `std::list`, `std::unordered_map`). Kept permanently.
- `tests/reference_book_test.cpp`: 32 GoogleTest unit tests for the reference book.
- `docs/CONTRACTS.md`: normative semantics (v0.3).
- `CMakeLists.txt`, `.github/workflows/ci.yml`: build, and CI.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOB_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Compilers: GCC 13 or later, Clang 17 or later. Sanitizer build: add `-DLOB_SANITIZE=ON`.

## What is proven

- The reference book's behaviour matches the event sequences in CONTRACTS §4 for submit, cancel, and modify, including the reject paths. Each behaviour has a named test.
- The global accounting identity and the per-order identity hold after every scripted operation (`check_invariants()`).
- Under the 32 tests, GCC debug and GCC with ASan+UBSan both pass

## What is not proven

- The fast book does not exist yet, so there is nothing to compare against the reference.
- The tests have not yet run under the real GoogleTest runner. The sandbox that produced this commit could not download GoogleTest, so the suite was built against a local GoogleTest-compatible shim. CI is the first real run.
- No randomized stream, no differential test, no fuzzing, no latency measurements.
- No stop orders, no simulator, no feed ingestion, no ring buffer.
- Self-trade prevention is specified as reserved but not implemented.

## Design notes

The reference book is kept on purpose as the oracle. Its clarity is the reason it is trusted, so it is not optimized. The fast book will replace the data structures, not the semantics.
