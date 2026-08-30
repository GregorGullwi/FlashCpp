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
- **`tests/*_e<ID>.cpp`** — Negative compile tests. One or more terminal
  `_e<number>` segments encode the exact expected `DiagnosticId` number
  multiset. The compiler must reject the source cleanly and emit exactly those
  IDs. Location, severity, diagnostic name, and note role are not asserted.
- **Encoded `tests/*_e<ID>.cpp` inventory** — Negative tests encode their exact
  diagnostic-ID multiset. Legacy `_fail.cpp` classification and inventory were
  removed at boundary 2F; new `_fail.cpp` names fail discovery.
- **`tests/expected_failures.tsv`** — Positive tests blocked by known compiler
  defects. Entries name the expected terminal stage and removal boundary;
  success or a different failure stage is stale. Negative tests cannot enter
  this manifest.
- **`tests/test_*_ret0.cpp`** — The most common pattern. Multiple sub-tests inside `main()` return distinct non-zero values on failure (e.g., `return 1`, `return 2`, …) so the exit code identifies which sub-test failed.
- Both runners discover every root `tests/*.cpp` file. Encoded names select
  negative compilation; files without `main` are compile-only tests.
- Tests requiring external C helper objects are listed in the `EXTRA_C_HELPERS` variable in `run_all_tests.sh`.
- Every bug fix should include a `_ret0.cpp` test that exercises the fix and returns 0 on success.
- Every new rejection test must use `_e<number>` filename segments. Do not add
  a legacy `_fail.cpp` test or a per-test diagnostic ID.
