# Standard Library Header Tests

Probes for C++ standard headers. They live here because many still fail or are slow; the default `tests/run_all_tests.ps1` full suite only scans `tests/*.cpp` (not this subdirectory).

For build/run commands, Windows vs Linux scripts, and the status table, see **[README_STANDARD_HEADERS.md](README_STANDARD_HEADERS.md)**.

## Quick start (Windows)

```powershell
.\build_flashcpp.bat
pwsh tests/run_all_tests.ps1 test_std_limits.cpp
```

## Documentation

- **`README_STANDARD_HEADERS.md`** — how to run tests, status table, current blockers
- **`STANDARD_HEADERS_MISSING_FEATURES.md`** — older feature-gap notes
