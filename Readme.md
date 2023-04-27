## Flash Cpp Compiler

Flash Cpp Compiler is a C++ compiler that focuses on compile speed rather than runtime speed.

It's been developed in heavy cooperation with ChatGPT to speed up development.

### Todo:

## 1. Preprocessor
   - [x] Implement the `#include` directive to allow including header files (2023-04-15)
   - [x] Implement the `#define` directive to allow defining macros (2023-04-07)
      - [x] Implement string concatenation in macro definitions (2023-04-15)
      - [x] Implement token pasting (2023-04-15)
      - [x] Implement variadic macros (2023-04-18)
      - [ ] Implement variadic optional macros
      - [x] Implement macro function-like syntax (2023-04-15)
      - [x] Implement conditional expressions within macro definitions (2023-04-12)
      - [ ] Implement support for built-in defines from clang
      - [x] Implement support for __has_include() (2023-04-15)
   - [x] Implement undefining macros with `#undef` (2023-04-12)
   - [x] Implement the `#ifdef` and `#ifndef` directives to allow conditional compilation (2023-04-08)
   - [x] Implement the `#if` directive to allow conditional compilation (2023-04-12)
   - [ ] Implement the `#error` directive to allow generating error messages
   - [ ] Conform to the C++ standard phases
   - Optimize the preprocessor for speed and memory usage (on-going)

## 2. Lexer
- [ ] Define the set of valid tokens for the language
- [ ] Implement a state machine to recognize each token
- [ ] Implement error handling to report invalid tokens and their location
- [ ] Optimize the lexer for speed and memory usage

## 3. Parser
- [ ] Define the grammar of the language
- [ ] Implement a parser to build an abstract syntax tree (AST)
- [ ] Implement error handling to report syntax errors and their location
- [ ] Optimize the parser for speed and memory usage

## 4. Semantic analysis
- [ ] Define the semantics of the language
- [ ] Implement type checking to ensure type safety
- [ ] Implement name resolution to resolve identifiers to their declarations
- [ ] Implement control flow analysis to detect unreachable code
- [ ] Implement error handling to report semantic errors and their location
- [ ] Optimize semantic analysis for speed and memory usage

## 5. IR generation
- [ ] Design an intermediate representation (IR) for the language
- [ ] Implement code generation from the AST to the IR
- [ ] Optimize the IR for speed and memory usage

## 6. Optimization techniques
- [ ] Implement optimization passes on the IR to improve runtime performance
- [ ] Optimize for code size or runtime speed as appropriate

## 7. Code generation
- [ ] Implement code generation from the IR to machine code
- [ ] Optimize the generated code for speed and memory usage

## 8. Runtime support
- [ ] Implement runtime support libraries such as a standard library or a garbage collector
- [ ] Optimize runtime support for speed and memory usage

## 9. Debugging support
- [ ] Implement debugging support such as generating debug symbols or providing a debugger interface
- [ ] Optimize debugging support for speed and memory usage

## 10. Integration tests
- [ ] Implement integration tests to verify that the compiler works correctly with sample programs
- [ ] Optimize the testing process for speed and effectiveness

## 11. Documentation
- [ ] Create documentation for the compiler, including usage instructions and API reference
- [ ] Optimize the documentation for clarity and completeness


[![Run on Repl.it](https://replit.com/badge/github/GregorGullwi/FlashCpp)](https://replit.com/new/github/GregorGullwi/FlashCpp)