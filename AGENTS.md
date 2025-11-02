# Repository Guidelines

## Project Structure & Module Organization
FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/FlashCppTest/...` with fixtures in `tests/Reference/`. Generated binaries belong in `x64/` or `Debug/` and stay untracked. Batch scripts, `FlashCpp.sln`, and the `Makefile` cover Windows and clang workflows.

## Build, Test, and Development Commands
- Use `make test CXX=clang++` — invokes all the tests in FlashCppTest.cpp
- Use `make main CXX=clang++` — builds the compiler, good when using bash as WSL/Linux, same as build_flashcpp.bat on a regular Windows terminal.
- `build_flashcpp.bat` — invokes MSBuild to produce `x64\Debug\FlashCpp.exe`; run it whenever you change compiler sources.
- Visual Studio IDE — open `FlashCpp.sln`, select `x64/Debug`, and build when you need MSVC debugging.
- `build_flashcpp_debug.bat` — compiles `tests\FlashCppDebugTest\flashcpp_debug_test.cpp` using the freshly built FlashCpp compiler and links it with MSVC `link.exe`, emitting binaries into `Debug\`.
- `link_and_run_add.bat`, `link_and_run_arithmetic.bat`, and `link_and_run_test_debug.bat` — build with FlashCpp, link via MSVC, and run quick regression checks.
- Clang workflow (WSL/Linux/macOS, experimental) — run `setup.sh`, then `make main` / `make test`; be ready to patch missing LLVM headers and register-allocator stubs before it links.

## Coding Style & Naming Conventions
Target warning-clean builds under both MSVC and clang. Use four-space indentation, same-line braces, and keep includes grouped `<system>` before quotes. Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase. Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler. Run clang-format or Visual Studio's formatter with LLVM style if available.

## Testing Guidelines
The doctest runner lives in `tests/FlashCppTest/FlashCppTest/FlashCppTest.cpp`. Add coverage with `TEST_CASE` blocks (e.g., `"Parser:IfWithInit"`). Run `link_and_run_test_debug.bat` for MSVC smoke tests, or `make test && ./x64/test` under clang. Shared expectations belong in `tests/Reference/`; document intentional skips inline.

## Workspace Hygiene
Delete binaries, dumps, and logs before you stage. Purge `x64/`, `Debug/`, `output/`, and any ad-hoc `.obj`, `.exe`, `.pdb`, or `.lst`; `git status --short` should show only intentional edits.

## Debugging & Reference Tips
`build_and_dump.bat` compares MSVC-generated objects against FlashCpp output via `dumpbin`, which is great for spotting codegen drift. When investigating parser issues, rebuild with `build_flashcpp.bat` and run `FlashCpp.exe -v path\to\input.cpp` to emit dependency and IR traces without editing source.
