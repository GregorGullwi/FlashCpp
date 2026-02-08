# Repository Guidelines

## Project Structure & Module Organization
FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/*.cpp`.
Generated binaries belong in `x64/` or `Debug/` and stay untracked.
Batch scripts, `FlashCpp.sln`, and the `Makefile` cover Windows and clang workflows.
- Use `tests/run_all_tests.ps1` to verify that your changes didn't break any existing functionality

## Build, Test, and Development Commands
- You are most likely running in a powershell, plan your calls accordingly.
- Path to the Windows SDK is "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\"
- `.\build_flashcpp.bat` — invokes MSBuild to produce `x64\Debug\FlashCpp.exe`; run it whenever you change compiler sources.
- To test an individual file in the test folder, you can use `"./tests/run_all_tests.ps1 [test_name].cpp"`
- Visual Studio IDE — open `FlashCpp.sln`, select `x64/Debug`, and build when you need MSVC debugging.
- Use `link.bat` to link with all required libraries — build with FlashCpp, link via MSVC, and run quick regression checks.
- If you call link.exe manually, ensure you include all necessary libraries: `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" /ENTRY:mainCRTStartup /LIBPATH:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"`

## Coding Style & Naming Conventions
Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes.
Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase.
Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler.
Call `emit` functions like `emitMovFromFrameBySize` instead of `generateMov`. Do not add opcodes manually to `textSectionData` in `IRConverter.h`, make helper functions if no fitting `emit` function exist.

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
Available categories: General, Parser, Lexer, Templates, Symbols, Types, Codegen, Scope, Mangling, All
To show IR, use `--log-level=Codegen:debug`

## Implementation Guidelines
* If you find an error with a macro definition that starts with a single underscore(_): This is often a sign of something else gone wrong in the preprocessor. Investigate the root cause! What does the C++20 standard say and define?
* If a needed change requires an architectural edit: Make a plan in form of a todo list.
* If you discover bugs in the compiler: document it as a todo or add it to docs/KNOWN_ISSUES.md
* If you fix a bug, make a test first in the tests/folder that demonstrates that behavior and submit it. Append a _retX.cpp to the file name and make the return value dependent on the non-buggy behavior, so we can easily test if the bug appears again later.
* If you want to submit a test that is supposed to fail, just add "_fail.cpp" at the end and the script will handle it
* Make proper implementation of features, don't just leave TODOs or skip tokens when parsing.