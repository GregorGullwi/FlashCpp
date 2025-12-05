# Repository Guidelines

## Project Structure & Module Organization
FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/*.cpp`. Generated binaries belong in `x64/` or `Debug/` and stay untracked. Batch scripts, `FlashCpp.sln`, and the `Makefile` cover Windows and clang workflows.

## Build, Test, and Development Commands
- Use `make main CXX=clang++` — builds the compiler, good when using bash as WSL/Linux, to produce `x64\Debug\FlashCpp`. Run it whenever you change compiler source files.
- Use `make test CXX=clang++` — invokes all the tests in FlashCppTest.cpp on WSL/Linux. 
- Clang workflow (WSL/Linux/macOS, experimental) — run `setup.sh`, then `make main` / `make test`; be ready to patch missing LLVM headers and register-allocator stubs before it links.
- There is also a "tests/run_all_tests.sh", which is a bit untested. Feel free to modify and fix any issues you find with it.
- `tests/test_reference_files.ps1` is run on each commit from a Github Actions file.

## Coding Style & Naming Conventions
Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes. Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase. Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler. Call `emitMov` functions instead of `generateMov` or adding mov opcodes manually to `textSectionData` in `IRConverter.h`.

## Testing Guidelines
The doctest runner lives in `tests/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp`. Add coverage with `TEST_CASE` blocks (e.g., `"Parser:IfWithInit"`). Run `link_and_run_test_debug.bat` for MSVC smoke tests, or `make test && ./x64/test` under WSL/Linux. Shared expectations belong in `tests/`; document intentional skips inline. Don't forget to add kernel32.lib to the link command line.

## Workspace Hygiene
Delete binaries, dumps, and logs before you summarize your work. Feel free to leave debug output in the source code. Purge `x64/`, `Debug/`, `output/`, and any ad-hoc `.obj`, `.exe`, `.pdb`, or `.lst`; `git status --short` should show only intentional edits.

## Debugging & Reference Tips
Use `dumpbin.exe`, locate the path with `where.exe`, which is great for spotting codegen drift. When investigating parser issues, rebuild with `build_flashcpp.bat` and run `x64/Debug/FlashCpp.exe -v path\to\input.cpp` to emit dependency and IR traces without editing source. Output file will end up in the working folder.

## Logging Configuration
Control log verbosity at runtime using command-line options:
- `--log-level=level` — Set global log level (error, warning, info, debug, trace, or 0-4)
- `--log-level=category:level` — Set log level for a specific category (e.g., Parser:trace, Codegen:debug)
To show IR, use `--log-level=Codegen:debug`
Available categories: General, Parser, Lexer, Templates, Symbols, Types, Codegen, Scope, Mangling, All