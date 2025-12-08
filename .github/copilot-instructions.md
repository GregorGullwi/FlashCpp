# Repository Guidelines

## Project Structure & Module Organization
FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/*.cpp`. Generated binaries belong in `x64/` or `Debug/` and stay untracked.
Bash scripts, and the `Makefile` cover clang workflows.
Use `cd /tmp && /home/runner/work/FlashCpp/FlashCpp/x64/Debug/FlashCpp [test_file_name].cpp -o [test_file_name].o` when compiling individual test files to verify your changes.
Link and run to verify the return output.

## Build, Test, and Development Commands
- Use `make main CXX=clang++` — builds the compiler, good when using bash as WSL/Linux, to produce `x64\Debug\FlashCpp`. Run it whenever you change compiler source files.
- Use `tests/run_all_tests.sh` — invokes all the tests in the tests folder on WSL/Linux. 
- `tests/test_reference_files.ps1` is run on each commit from a Github Actions file.

## Coding Style & Naming Conventions
Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes.
Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase.
Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler.
Call `emit` functions like `emitMovFromFrameBySize` instead of `generateMov`. Do not add opcodes manually to `textSectionData` in `IRConverter.h`, make helper functions if no fitting `emit` function exist.

## Testing Guidelines
The doctest runner lives in `tests/FlashCppTest/FlashCppTest/FlashCppTest/FlashCppTest.cpp`. Add coverage with `TEST_CASE` blocks (e.g., `"Parser:IfWithInit"`). Run `link_and_run_test_debug.bat` for MSVC smoke tests, or `make test && ./x64/test` under WSL/Linux. Shared expectations belong in `tests/`; document intentional skips inline. Don't forget to add kernel32.lib to the link command line.
When adding new test cases and files, verify that they are valid C++20 source file by compiling them with clang first in c++20 mode.

## Workspace Hygiene
Delete binaries, dumps, and logs before you summarize your work. Feel free to leave debug output in the source code. Purge `x64/`, `Debug/`, `output/`, and any ad-hoc `.obj`, `.exe`, `.pdb`, or `.lst`; `git status --short` should show only intentional edits.

## Debugging & Reference Tips
Use `dumpbin.exe`, locate the path with `where.exe`, which is great for spotting codegen drift. When investigating parser issues, rebuild with `build_flashcpp.bat` and run `x64/Debug/FlashCpp.exe -v path\to\input.cpp` to emit dependency and IR traces without editing source. Output file will end up in the working folder.

## Logging Configuration
- Use FLASH_LOG_FORMAT(cat, level, fmt, ...) - uses std::format for cleaner syntax when all arguments are formattable
- For other use cases use FLASH_LOG(cat, level, ...) - uses operator<< for maximum compatibility
Control log verbosity at runtime using command-line options:
- `--log-level=level` — Set global log level (error, warning, info, debug, trace, or 0-4)
- `--log-level=category:level` — Set log level for a specific category (e.g., Parser:trace, Codegen:debug)
To show IR, use `--log-level=Codegen:debug`
Available categories: General, Parser, Lexer, Templates, Symbols, Types, Codegen, Scope, Mangling, All