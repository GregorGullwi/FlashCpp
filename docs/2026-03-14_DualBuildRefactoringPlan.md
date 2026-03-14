# Dual Build System Refactoring Plan
Date: 2026-03-14

## Overview
Enable FlashCpp to build both as a unity build (current) and as individual translation units, without changing the flat folder structure.

## 1. Filename Review

### Reasonably Named (after header merge)
- `Parser.h/cpp`, `Lexer.h/cpp`
- `IrGenerator.h` (renamed from CodeGen.h), `IRConverter.h` (merge split headers into it)
- `AstNodeTypes.h`, `IRTypes.h`
- `CompileContext.h`, `CommandLineParser.h`

### Needs Renaming
| File | Issue | Suggestion |
|------|-------|------------|
| `FlashCppUnity.h` | Misleading name | Rename to `CompilerIncludes.h` |
| `CodeGen.h` | No "CodeGen" class - it's the IrGenerator phase | Rename to `IrGenerator.h` |

### Also Rename (for consistency with new naming)
| File | Action |
|------|--------|
| `CodeGen_*.cpp` | Rename to `IrGenerator_*.cpp` (or keep, but update plan references) |

### Inconsistent Naming - Headers Should Be Consolidated
The headers are currently SPLIT into multiple continuation files (bad). CPPs can have suffixes (good).

#### Headers to MERGE into single files:

**IRConverter.h** - merge these 18 files:
- IRConverter_Conv_*.h (8 files: VarDecl, Memory, CorePrivate, Calls, Arithmetic, Fields, ControlFlow, EHSeh, Convert)
- IRConverter_Emit_*.h (6 files: CompareBranch, EHSeh, MovLoadStore, FloatSIMD, CallReturn, ArithmeticBitwise)
- IRConverter_ABI.h
- IRConverter_Encoding.h
- IRConverter_ConvertMain.h

**Other headers:**
- `IROperandHelpers.h` → Rename to `IrOperandHelpers.h` for consistency

### Keep Split: .cpp Files
| Pattern | Status |
|---------|--------|
| `Parser_*.cpp` | Good - keep split |
| `CodeGen_*.cpp` → `IrGenerator_*.cpp` | Good - keep split (rename for consistency) |

## 2. Inline Functions to Move to .cpp

| Header | Action |
|--------|--------|
| `IrType.h` | Create `IrType.cpp`, move inline implementations |
| `TypeTraitEvaluator.h` | Create `TypeTraitEvaluator.cpp`, move inline implementations |

## 3. Large .cpp Files That Could Be Split

The headers are already well-split as continuations of each other, but the user wants to MERGE headers (not split further). Focus on large .cpp files:

### Consider Splitting (>3000 lines)
| File | Lines | Suggested Split |
|------|-------|-----------------|
| `Parser_Templates_Inst_ClassTemplate.cpp` | 6556 | Split by template kind (inheritance, specialization, deduction) |
| `Parser_Expr_PrimaryExpr.cpp` | 5693 | Split by expression type |
| `Parser_Templates_Class.cpp` | 5432 | Split class templates from function templates |
| `Parser_Decl_StructEnum.cpp` | 4279 | Split struct, enum, union into separate files |
| `CodeGen_Expr_Operators.cpp` | 4071 | Split binary vs unary operators |

### Notes
- Splitting is optional - the dual build will work without it
- Large .cpp files are harder to navigate and compile separately

## 4. Dual Build Implementation

### A. Keep CompilerIncludes.h (renamed from FlashCppUnity.h)
Remains as-is for unity build compatibility. Unity build remains the DEFAULT.

### B. Add Proper Header Declarations
Since headers are merged (not split like .cpp files), minimal new headers needed:
- `Globals.h` - extern declarations for globals currently in Globals.cpp
- Ensure existing headers have complete declarations for modular compilation

### C. Update FlashCpp.vcxproj
Add all .cpp files as ClCompile items:
```xml
<ClCompile Include="src\Parser.cpp" />
<ClCompile Include="src\Parser_Core.cpp" />
<ClCompile Include="src\Parser_Declarations.cpp" />
<!-- ... all ~80 .cpp files -->
```

### D. Update Makefile
Unity build remains DEFAULT. Add modular build target:
```makefile
# Default: unity build (faster compilation)
main: $(MAIN_TARGET)

# Modular build (individual .cpp files - for separate compilation testing)
modular: $(OBJS)
	$(CXX) $(OBJS) -o $(MAIN_TARGET)

SRCS := $(wildcard src/*.cpp)
OBJS := $(SRCS:.cpp=.o)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
```

### E. Handle Global State
- Ensure `Globals.cpp` compiles as standalone
- Create `Globals.h` with extern declarations for shared globals

## 5. Build Mode Detection
Use preprocessor to toggle between builds:
```cpp
#ifdef UNITY_BUILD
    #include "Parser_Core.cpp"  // Unity: include directly
#endif
```
Pass `-DUNITY_BUILD` in unity mode, omit for modular.

## 6. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Circular includes | Add forward declarations, careful include ordering |
| Include order dependencies | Document required include order in headers |
| Build time increase | Modular build is optional; unity remains default |

## 7. Execution Order

1. **MERGE split headers into single headers** (before any other changes)
   - Merge `IRConverter_Conv_*.h`, `IRConverter_Emit_*.h`, etc. into `IRConverter.h`
   - Merge `CodeGen_*.h` into `IrGenerator.h`
   - Merge `Parser_*.h` into `Parser.h`
2. Rename `CodeGen.h` → `IrGenerator.h` (update all #include references)
3. Rename `FlashCppUnity.h` → `CompilerIncludes.h`
4. Create `IrType.cpp` and `TypeTraitEvaluator.cpp` with inline implementations
5. Create minimal `.h` files for key split `.cpp` files as needed
6. Update FlashCpp.vcxproj with all .cpp files
7. Update Makefile with modular target
8. Test both build modes
9. Run benchmarking script

## 8. CodeGen → IrGenerator Rename Details

Files to update:
- `src/CodeGen.h` → `src/IrGenerator.h`
- Update `#include "CodeGen.h"` in 17 files:
  - All `CodeGen_*.cpp` files (15 files)
  - `AstToIr.h`
  - `FlashCppUnity.h`
- Optionally rename `CodeGen_*.cpp` → `IrGenerator_*.cpp` for consistency

## 9. Benchmarking Strategy

### Goal
Measure whether unity vs modular builds differ significantly in compile time.

### Method
Create `benchmark_builds.ps1` script that:
1. Cleans build directory
2. Measures time for unity build: `measure-command { & .\build_flashcpp.bat }`
3. Cleans build directory  
4. Measures time for modular build: `make clean && make modular`
5. Reports difference

### Expected Results
- Unity build: ~30-60s (typical for 50k+ lines of template-heavy code)
- Modular build: Expected slower due to:
  - Multiple compilation passes
  - Less opportunity for inline optimization
  - Additional object file linking

### If Modular is Faster
Unexpected - would indicate better parallelization opportunities.

### If Unity is Faster (Expected)
Typical for C++ template-heavy projects - compiler can inline across translation units and reduces redundant template instantiations.

### Platform Differences
- MSVC: May show larger gap
- Clang: May show smaller gap
