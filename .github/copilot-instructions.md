# Repository Guidelines

## Project Structure & Module Organization
FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/*.cpp`. Generated binaries belong in `x64/` or `Debug/` and stay untracked.
Bash scripts, and the `Makefile` cover clang workflows.
Use `cd /tmp && /home/runner/work/FlashCpp/FlashCpp/x64/Debug/FlashCpp [test_file_name].cpp -o [test_file_name].o` when compiling individual test files to verify your changes.
Link and run to verify the return output.

## Build, Test, and Development Commands
- Use `make main CXX=clang++` — builds the compiler, good when using bash as WSL/Linux, to produce `x64\Debug\FlashCpp`. Run it whenever you change compiler source files.
- Use `tests/run_all_tests.sh` — invokes all the tests in the tests folder on WSL/Linux. 

## Coding Style & Naming Conventions
Try to reuse as much code as possible. Look in the same PR for similar code patterns and try to extract it into a function or a local lambda.
For patterns that are used in different places in a compiler, like parsing attributes, skipping balanced braces, brackets, etc, search for existing helper functions and try to use those.
Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes.
Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase.
Prefer `std::string_view` for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler.
Prefer StringBuilder instead of using std::string concatination.
Call `emit` functions like `emitMovFromFrameBySize` instead of `generateMov`. Do not add opcodes manually to `textSectionData` in `IRConverter.h`, make helper functions if no fitting `emit` function exist.

## Testing Guidelines
When adding new test cases and files, verify that they are valid C++20 source file by compiling them with clang first in c++20 mode.

## Workspace Hygiene
Delete binaries, dumps, and logs before you summarize your work. Feel free to leave debug output in the source code.
Purge `x64/`, `Debug/`, `output/`, and any ad-hoc `.obj`, `.exe`, `.pdb`, or `.lst`; `git status --short` should show only intentional edits.

## Debugging & Reference Tips
Use `strace` to get more information regarding coredumps, since gdb or lldb isn't available. Prefer logging.
Run `FlashCpp with -v` to emit dependency and IR traces without editing source. Output file will end up in the working folder.

## Logging Configuration
- Use FLASH_LOG_FORMAT(cat, level, fmt, ...) - uses std::format for cleaner syntax when all arguments are formattable
- Use IS_FLASH_LOG_ENABLED(cat, level) if you need a loop to gather information before the log macro.
- For other use cases use FLASH_LOG(cat, level, ...) - uses operator<< for maximum compatibility
Control log verbosity at runtime using command-line options:
- `--log-level=level` — Set global log level (error, warning, info, debug, trace, or 0-4)
- `--log-level=category:level` — Set log level for a specific category (e.g., Parser:trace, Codegen:debug)
To show IR, use `--log-level=Codegen:debug`
Available categories: General, Parser, Lexer, Templates, Symbols, Types, Codegen, Scope, Mangling, All

## Implementation Guidelines
* If you find an error with a macro definition that starts with a single underscore(_): This is often a sign of something else gone wrong in the preprocessor. Investigate the root cause! What does the C++20 standard say and define?
* If a needed change requires an architectural edit: Make a plan in form of a todo list.
* If you discover bugs in the compiler: document it as a todo or add it to docs/KNOWN_ISSUES.md
* If you fix a bug, make a test first in the tests/folder that demonstrates that behavior and submit it. Append a _retX.cpp to the file name and make the return value dependent on the non-buggy behavior, so we can easily test if the bug appears again later.