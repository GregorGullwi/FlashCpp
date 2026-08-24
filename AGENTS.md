# Repository Guidelines

## Project Structure & Module Organization
- FlashCpp is a C++20 compiler front-end. Core sources live in `src/`; tests sit in `tests/*.cpp`.
- Generated binaries belong in `x64/` or `Sharded/` and stay untracked.
- Batch scripts, `FlashCpp.sln`, and the `Makefile` cover Windows and clang workflows.
- Use `pwsh tests/run_all_tests.ps1` to verify that your changes didn't break any existing functionality, but never run it in parallell with build_flashcpp.bat! Expect it to take 5 minutes to run all tests.
- When creating PRs, use descriptive language to highlight what problems it solves. If you want to highlight testing, mension added test files and problems they protect against, don't just list test commands you (always) run.

## Build, Test, and Development Commands
- You are most likely running in a powershell (pwsh), plan your calls accordingly.
- Path to the Windows SDK is "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\"
- `.\build_flashcpp.bat` — invokes MSBuild to produce `x64\Sharded\FlashCppMSVC.exe`; run it whenever you change compiler sources.
- To test an individual file in the test folder, you can use `"./tests/run_all_tests.ps1 [test_name].cpp"`

## Coding Style & Naming Conventions
- Try to reuse as much code as possible. Look in the same PR for similar code patterns and try to extract it into a function.
- For patterns that are used in different places in a compiler, like parsing attributes, skipping balanced braces, brackets, etc, search for existing helper functions and try to use those.
- Target warning-clean builds under both MSVC and clang. Use tab indentation, same-line braces, and keep includes grouped `<system>` before quotes.
- When checking indentation in touched lines, prefer explicit tab rendering or `git diff --check`; normal file views can hide off-by-one tab mistakes.
- Types (`AstToIr`, `ChunkedAnyVector`) use PascalCase; functions and methods stay camelCase.
- Prefer StringHandle primarily or `std::string_view` secondary for non-owning parameters, follow the existing enum/class organization, and reach for branchless patterns (conditional moves, bit masks) when they keep IR simpler.
- Prefer StringBuilder instead of using std::string concatination, apart from when throwing exceptions, since we don't care about performance in that context.
- Call `emit` functions like `emitMovFromFrameBySize` instead of `generateMov`. Do not add opcodes manually to `textSectionData` in `IRConverter.h`, make helper functions if no fitting `emit` function exist.
- Never use default parameter values in function or method signatures in the codebase, tests are excluded from this rule. Every argument must be passed explicitly by the caller. Default parameters hide misuse (e.g., forgetting to propagate a flag) and make call sites silently wrong instead of producing a compile error.
- Multi-line comments should have the same indentation as the code it describes.
- Avoid shared_ptr and unique_ptr. Prefer `FrontendContext`-owned typed arenas
  and stable IDs. A `ChunkedVector<T>` arena must be pinned because copying or
  moving the arena can invalidate element addresses; stable addresses are an
  implementation property, not semantic identity.

## Memory, Arenas & Stack Usage
- REALLY try to minimize memory footprint. Use bitmasks instead of bools. Think about struct padding.
- Use `InlineVector` only when the inline capacity is supported by measured
  element counts and spill rates. Avoid large local `InlineVector` instances
  on recursive parser, template, substitution, constraint, lookup, constexpr,
  or semantic paths.
- Treat `ChunkedVector<T>`'s `ChunkSize` as an element count and pass an
  explicit measured value for new arenas. Do not rely on its current default.
- `StringHandle` is for interned spelling and lookup keys. Never use its numeric
  value as declaration, type, template, specialization, cache, or ABI identity.
- Do not add new semantic objects to `ChunkedAnyVector`; it is a legacy AST
  bridge.
- Unbounded source-language recursion must use an explicit worklist or
  arena-owned frame stack. Do not map template instantiation depth, constraint
  recursion, substitution recursion, or point-of-instantiation processing
  directly to native C++ call depth.
- Keep recursive-path frames small: pass IDs or spans instead of AST values,
  template environments, argument vectors, or other large objects by value.
- For changes on potentially recursive paths, inspect stack-usage output when
  the toolchain supports it (`-fstack-usage` on clang/GCC), compare changed
  frames against the baseline, and include a deep targeted regression under
  the normal OS stack limit. Do not raise the stack limit to make the new path
  pass.
- Pull request review must report the largest changed stack frame for recursive
  paths and whether native stack usage remains bounded as logical template
  depth increases.

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
* If you encounter existing bugs while testing, notify the user. If it's close to the area you are already working on, make an effort to investigste and fix it.
* Try to make complete C++20 standard compliant solutions. If you deviate from that, notify the user and make a TODO.
* Do not hardcode standard library or vendor STL template names, helper names, macros, or implementation details in compiler logic to make a standard-header test pass. Fix the underlying C++ language mechanism instead, such as parsing, lookup, substitution, partial specialization ordering, constraint handling, alias/member-type materialization, instantiation caching, or diagnostics. Only add name-based handling for constructs that are genuinely compiler intrinsics or builtins, and document why the C++ standard or target ABI requires that treatment.
* For failures found through `<type_traits>`, `<ranges>`, or other standard headers, create or identify a reduced non-`std` regression that demonstrates the same language rule before implementing the fix. The compiler fix should pass because the generic mechanism works, not because it recognizes a library spelling such as `std::remove_cv`, `_Remove_cvref_t`, or an MSVC STL helper.
* Before committing a standard-header fix, inspect the compiler-source diff for `std::`, `_MSVC`, vendor-reserved identifiers, and exact standard-library helper names. If any appear in implementation code, either remove the special case or explicitly justify it as a builtin/intrinsic path in the code and PR summary.
* Avoid coding in fallback paths. Invalid cases should throw InternalError or CompileError.
* When creating tests, mix different native types and sizes with structs and templates to get good coverage of each feature.
