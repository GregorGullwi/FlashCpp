# Compliance
All code should be compliant with the C++20 standard. Evaluate everything through the language specification and grammar lense.

# Formatting
All `.h` and `.cpp` source files use **hard tabs** (not spaces) for indentation.

# Auto fix
Suggest auto fixes when possible. But be careful when crafting the diff so all the brackets and indentation line upp correctly.

# Performane
Keep your eye out for bad performance characteristics and suggest performant solutions.

# Architechure
How does the change fit into the overall compiler and architecture? Is code generation doing lookups or fallbacks due missing logic in the semantic analyser? Or is the parser doing work that the semantic analyzer should be doing?

# Test File Conventions

- **`tests/test_*_ret<N>.cpp`** — Runtime tests. The test must compile, link, run, and `main()` must return `<N>`. The suffix encodes the expected return value. A return mismatch fails CI.
- **`tests/test_*_fail.cpp`** — Negative compile tests. The compiler is expected to **reject** the file (produce a compile error). If it compiles successfully, CI reports `[UNEXPECTED PASS]` and fails.
- **`tests/test_*_ret0.cpp`** — The most common pattern. Multiple sub-tests inside `main()` return distinct non-zero values on failure (e.g., `return 1`, `return 2`, …) so the exit code identifies which sub-test failed.
- Test files are auto-discovered by `tests/run_all_tests.sh` (Linux) and `tests/run_all_tests.ps1` (Windows) by scanning for `int main(` in `tests/*.cpp`.
- Tests requiring external C helper objects are listed in the `EXTRA_C_HELPERS` variable in `run_all_tests.sh`.
- Every bug fix should include a `_ret0.cpp` test that exercises the fix and returns 0 on success.
- Every feature that should be rejected by the compiler should have a `_fail.cpp` test.
