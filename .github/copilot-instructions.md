# Repository Guidelines

## Project Structure & Module Organization
FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/FlashCppTest/...` with fixtures in `tests/Reference/`. Generated binaries belong in `x64/` or `Debug/` and stay untracked. Batch scripts, `FlashCpp.sln`, and the `Makefile` cover Windows and clang workflows.

## Build, Test, and Development Commands
- Path to the Windows SDK is "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\"
- `.\build_flashcpp.bat` — invokes MSBuild to produce `x64\Debug\FlashCpp.exe`; run it whenever you change compiler sources.
- Visual Studio IDE — open `FlashCpp.sln`, select `x64/Debug`, and build when you need MSVC debugging.
- Use `link.bat` to link with all required libraries — build with FlashCpp, link via MSVC, and run quick regression checks. If you call link.exe manually, ensure you include all necessary libraries: `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" /LIBPATH:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"`
- Clang workflow (WSL/Linux/macOS, experimental) — run `setup.sh`, then `make main` / `make test`; be ready to patch missing LLVM headers and register-allocator stubs before it links.
- Use `make test CXX=clang++` — invokes all the tests in FlashCppTest.cpp on WLS/Linux. 
- Use `make main CXX=clang++` — builds the compiler, good when using bash as WSL/Linux, same as .\build_flashcpp.bat on a regular Windows terminal.

## Coding Style & Naming Conventions
Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes. Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase. Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler.

## Testing Guidelines
The doctest runner lives in `tests/FlashCppTest/FlashCppTest/FlashCppTest.cpp`. Add coverage with `TEST_CASE` blocks (e.g., `"Parser:IfWithInit"`). Run `link_and_run_test_debug.bat` for MSVC smoke tests, or `make test && ./x64/test` under WLS/Linux. Shared expectations belong in `tests/Reference/`; document intentional skips inline. Don't forget to add kernel32.lib to the link command line.

## Workspace Hygiene
Delete binaries, dumps, and logs before you summarize your work. Feel free to leave debug output in the source code. Purge `x64/`, `Debug/`, `output/`, and any ad-hoc `.obj`, `.exe`, `.pdb`, or `.lst`; `git status --short` should show only intentional edits.

## Debugging & Reference Tips
`build_and_dump.bat` compares MSVC-generated objects against FlashCpp output via `dumpbin`, which is great for spotting codegen drift. When investigating parser issues, rebuild with `build_flashcpp.bat` and run `x64/Debug/FlashCpp.exe -v path\to\input.cpp` to emit dependency and IR traces without editing source.
