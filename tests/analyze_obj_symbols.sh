#!/bin/bash
# Linux script to analyze object file symbols using objdump
# Usage: ./analyze_obj_symbols.sh <file.o>

if [ $# -eq 0 ]; then
    echo "Usage: $0 <file.o>"
    exit 1
fi

OBJ_FILE="$1"

if [ ! -f "$OBJ_FILE" ]; then
    echo "ERROR: File not found: $OBJ_FILE"
    exit 1
fi

echo "=============================================="
echo "Object File Symbol Analysis"
echo "=============================================="
echo "File: $OBJ_FILE"
echo ""

# Check what tools are available
if command -v objdump &> /dev/null; then
    DUMP_TOOL="objdump"
elif command -v llvm-objdump &> /dev/null; then
    DUMP_TOOL="llvm-objdump"
else
    echo "ERROR: objdump not found. Please install binutils."
    exit 1
fi

echo "Using: $DUMP_TOOL"
echo ""

# Show file format info
echo "=== FILE HEADER ===" 
file "$OBJ_FILE"
echo ""

# Show all symbols
echo "=== ALL SYMBOLS ==="
$DUMP_TOOL -t "$OBJ_FILE"
echo ""

# Analyze and categorize symbols
echo "=== SYMBOL SUMMARY ==="

# Defined symbols (in .text, .data, .rodata sections)
DEFINED_COUNT=$($DUMP_TOOL -t "$OBJ_FILE" | grep -E '\s(\.text|\.data|\.rodata)\s' | wc -l)
echo "Defined symbols: $DEFINED_COUNT"
$DUMP_TOOL -t "$OBJ_FILE" | grep -E '\s(\.text|\.data|\.rodata)\s' | head -20

echo ""

# Undefined symbols (marked with *UND*)
UNDEFINED_COUNT=$($DUMP_TOOL -t "$OBJ_FILE" | grep '\*UND\*' | wc -l)
echo "Undefined symbols (need linking): $UNDEFINED_COUNT"
$DUMP_TOOL -t "$OBJ_FILE" | grep '\*UND\*' | head -20

if [ $UNDEFINED_COUNT -gt 20 ]; then
    echo "... and $(($UNDEFINED_COUNT - 20)) more"
fi

echo ""

# Lambda-related symbols
echo "=== LAMBDA-RELATED SYMBOLS ==="
LAMBDA_COUNT=$($DUMP_TOOL -t "$OBJ_FILE" | grep -i 'lambda' | wc -l)
echo "Total lambda symbols: $LAMBDA_COUNT"
$DUMP_TOOL -t "$OBJ_FILE" | grep -i 'lambda' | head -30

if [ $LAMBDA_COUNT -gt 30 ]; then
    echo "... and $(($LAMBDA_COUNT - 30)) more"
fi

echo ""

# Show sections
echo "=== SECTIONS ==="
$DUMP_TOOL -h "$OBJ_FILE"
echo ""

# Show disassembly of .text section (first 50 lines)
echo "=== DISASSEMBLY (.text section, first 50 lines) ==="
$DUMP_TOOL -d "$OBJ_FILE" | head -50
echo ""

# If it's a COFF file (Windows obj), try different approach
if file "$OBJ_FILE" | grep -q "COFF"; then
    echo "=== COFF-SPECIFIC ANALYSIS ==="
    
    # Try using nm if available
    if command -v nm &> /dev/null; then
        echo "Defined symbols (nm -g):"
        nm -g "$OBJ_FILE" | grep -v " U " | head -20
        echo ""
        
        echo "Undefined symbols (nm -u):"
        nm -u "$OBJ_FILE" | head -20
        echo ""
    fi
fi

# Detailed symbol table with demangling if c++filt is available
if command -v c++filt &> /dev/null; then
    echo "=== DEMANGLED SYMBOLS ==="
    $DUMP_TOOL -t "$OBJ_FILE" | c++filt | grep -E '(lambda|operator)' | head -30
    echo ""
fi
