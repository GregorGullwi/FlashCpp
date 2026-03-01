#pragma once

#include <string>
#include <numeric>
#include <vector>
#include <array>
#include <variant>
#include <string_view>
#include <span>
#include <stdexcept>
#include <assert.h>
#include <unordered_map>
#include <type_traits>

#include "CompileError.h"
#include "IRTypes.h"
#include "ObjFileWriter.h"
#include "ElfFileWriter.h"
#include "ProfilingTimer.h"
#include "ChunkedString.h"

// Global exception handling control (defined in main.cpp)
extern bool g_enable_exceptions;

// Enable detailed profiling by default in debug builds
// Can be overridden by defining ENABLE_DETAILED_PROFILING=0 or =1
#ifndef ENABLE_DETAILED_PROFILING
#define ENABLE_DETAILED_PROFILING 0
#endif

/*
	+ ---------------- +
	| Parameter 2	| [rbp + 24] < -Positive offsets
	| Parameter 1	| [rbp + 16] < -Positive offsets
	| Return Address| [rbp + 8]
	| Saved RBP		| [rbp + 0] < -RBP points here
	| Local Var 1	| [rbp - 8] < -Negative offsets
	| Local Var 2	| [rbp - 16] < -Negative offsets
	| Temp Var 1	| [rbp - 24] < -Negative offsets
	+ ---------------- +
*/

// Maximum possible size for mov instructions:
// - Regular integer mov: REX (1) + Opcode (1) + ModR/M (1) + SIB (1) + Disp32 (4) = 8 bytes
// - SSE float mov: Prefix (1) + REX (1) + Opcode (2) + ModR/M (1) + Disp32 (4) = 9 bytes
static constexpr size_t MAX_MOV_INSTRUCTION_SIZE = 9;

// x86-64 REX prefix byte constants
// REX format: 0100WRXB
// W = 64-bit operand size, R = ModR/M reg extension, X = SIB index extension, B = ModR/M r/m extension
static constexpr uint8_t REX_BASE = 0x40;  // REX prefix with no bits set (enables uniform byte regs)
static constexpr uint8_t REX_B    = 0x41;  // REX.B - extends r/m or base register to R8-R15
static constexpr uint8_t REX_X    = 0x42;  // REX.X - extends SIB index register
static constexpr uint8_t REX_R    = 0x44;  // REX.R - extends ModR/M reg field to R8-R15
static constexpr uint8_t REX_W    = 0x48;  // REX.W - 64-bit operand size
static constexpr uint8_t REX_WB   = 0x49;  // REX.W + REX.B
static constexpr uint8_t REX_WR   = 0x4C;  // REX.W + REX.R


// ============================================================================
// Shard includes - split from the original monolithic IRConverter.h
// ============================================================================

// Encoding helpers: OpCodeWithSize, generate* functions
#include "IRConverter_Encoding.h"

// ABI: Win64/SysV register maps, ABI helper templates, RegisterAllocator
#include "IRConverter_ABI.h"

// Free emit functions: MOV/Load/Store/LEA emit helpers
#include "IRConverter_Emit_MovLoadStore.h"

// Free emit functions: Float/SIMD emit helpers
#include "IRConverter_Emit_FloatSIMD.h"

// Free emit functions: Arithmetic/Bitwise emit helpers
#include "IRConverter_Emit_ArithmeticBitwise.h"

// Free emit functions: Call/Return/Push/Pop emit helpers
#include "IRConverter_Emit_CallReturn.h"

// IrToObjConverter class (includes IRConverter_Emit_CompareBranch.h and IRConverter_Emit_EHSeh.h internally)
#include "IRConverter_ConvertMain.h"
