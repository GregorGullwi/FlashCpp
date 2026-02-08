# Repository Guidelines

## Project Structure, Module Organization and Development Commands
- FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/*.cpp`.
- Generated binaries belong in `x64/` or `Debug/` and stay untracked.
- Use `make main CXX=clang++` — builds the compiler, good when using bash as WSL/Linux, same as .\build_flashcpp.bat on a regular Windows terminal.
- Use `tests/run_all_tests.sh` to verify that your changes didn't break any existing functionality

## Coding Style & Naming Conventions
Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes.
Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase.
Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler.
Call `emit` functions like `emitMovFromFrameBySize` instead of `generateMov`. Do not add opcodes manually to `textSectionData` in `IRConverter.h`, make helper functions if no fitting `emit` function exist.
When adding very common parsing code like parameter parsing, bracket/braces skipping, etc search for existing helper function.

## Workspace Hygiene
Delete binaries, dumps, and logs before you summarize your work. Feel free to leave debug output in the source code. Purge `x64/`, `Debug/`, `output/`, and any ad-hoc `.obj`, `.exe`, `.pdb`, or `.lst`; `git status --short` should show only intentional edits.

## Debugging & Reference Tips
Run `FlashCpp with -v` to emit dependency and IR traces without editing source. Output file will end up in the working folder.

## Logging Configuration
- Use FLASH_LOG_FORMAT(cat, level, fmt, ...) - uses std::format for cleaner syntax when all arguments are formattable
- Use IS_FLASH_LOG_ENABLED(cat, level) if you need a loop to gather information before the log macro.
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