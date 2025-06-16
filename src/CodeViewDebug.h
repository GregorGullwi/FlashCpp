#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

// CodeView debug information structures and constants
// Based on Microsoft CodeView format specification

namespace CodeView {

// CodeView signature for .debug$S sections (CV_SIGNATURE_C13)
constexpr uint32_t DEBUG_S_SIGNATURE = 0x4;

// CodeView signature for .debug$T sections
constexpr uint32_t DEBUG_T_SIGNATURE = 0x4;

// Debug subsection types for .debug$S
enum class DebugSubsectionKind : uint32_t {
    None = 0,
    Symbols = 0xF1,
    Lines = 0xF2,
    StringTable = 0xF3,
    FileChecksums = 0xF4,
    FrameData = 0xF5,
    InlineeLines = 0xF6,
    CrossScopeImports = 0xF7,
    CrossScopeExports = 0xF8,
};

// Symbol record types
enum class SymbolKind : uint16_t {
    S_END = 0x0006,
    S_FRAMEPROC = 0x1012,
    S_OBJNAME = 0x1101,
    S_COMPILE3 = 0x113C,
    S_ENVBLOCK = 0x113D,
    S_LOCAL = 0x113E,
    S_GPROC32 = 0x1110,
    S_GPROC32_ID = 0x1147,
    S_LPROC32 = 0x110F,
    S_REGREL32 = 0x1111,
    S_DEFRANGE_FRAMEPOINTER_REL = 0x1142,
    S_BUILDINFO = 0x114C,
    S_PROC_ID_END = 0x114F,
};

// Type record types
enum class TypeRecordKind : uint16_t {
    LF_POINTER = 0x1002,
    LF_PROCEDURE = 0x1008,
    LF_ARGLIST = 0x1201,
    LF_BUILDINFO = 0x1603,
    LF_SUBSTR_LIST = 0x1604,
    LF_STRING_ID = 0x1605,
    LF_UDT_SRC_LINE = 0x1606,
    LF_UDT_MOD_SRC_LINE = 0x1607,
    LF_FUNC_ID = 0x1601,
};

// Basic type indices (built-in types)
enum class SimpleTypeKind : uint32_t {
    None = 0x0000,
    Void = 0x0003,
    NotTranslated = 0x0007,
    HResult = 0x0008,
    
    SignedCharacter = 0x0010,
    UnsignedCharacter = 0x0020,
    NarrowCharacter = 0x0070,
    WideCharacter = 0x0071,
    Character16 = 0x007A,
    Character32 = 0x007B,
    
    SByte = 0x0068,
    Byte = 0x0069,
    Int16Short = 0x0011,
    UInt16Short = 0x0021,
    Int16 = 0x0072,
    UInt16 = 0x0073,
    Int32Long = 0x0012,
    UInt32Long = 0x0022,
    Int32 = 0x0074,
    UInt32 = 0x0075,
    Int64Quad = 0x0013,
    UInt64Quad = 0x0023,
    Int64 = 0x0076,
    UInt64 = 0x0077,
    Int128Oct = 0x0014,
    UInt128Oct = 0x0024,
    Int128 = 0x0078,
    UInt128 = 0x0079,
    
    Float16 = 0x0046,
    Float32 = 0x0040,
    Float32PartialPrecision = 0x0045,
    Float48 = 0x0044,
    Float64 = 0x0041,
    Float80 = 0x0042,
    Float128 = 0x0043,
};

// Structure for debug subsection header
#pragma pack(push, 1)
struct DebugSubsectionHeader {
    DebugSubsectionKind kind;
    uint32_t length;
};
#pragma pack(pop)

// Structure for symbol record header
struct SymbolRecordHeader {
    uint16_t length;  // Length of the record, not including this field
    SymbolKind kind;
};

// Structure for type record header
struct TypeRecordHeader {
    uint16_t length;  // Length of the record, not including this field
    TypeRecordKind kind;
};

// File checksum entry
#pragma pack(push, 1)
struct FileChecksumEntry {
    uint32_t file_name_offset;  // Offset into string table
    uint8_t checksum_size;
    uint8_t checksum_kind;      // 0 = None, 1 = MD5, 2 = SHA1, 3 = SHA256
    // Followed by checksum bytes
};
#pragma pack(pop)

// Line number entry
#pragma pack(push, 1)
struct LineNumberEntry {
    uint32_t offset;            // Offset from start of function
    uint32_t line_start : 24;   // Starting line number
    uint32_t delta_line_end : 7; // Delta to ending line number
    uint32_t is_statement : 1;   // 1 if this is a statement
};
#pragma pack(pop)

// Column number entry (optional)
struct ColumnNumberEntry {
    uint16_t start_column;
    uint16_t end_column;
};

// Line info header
#pragma pack(push, 1)
struct LineInfoHeader {
    uint32_t code_offset;       // Offset of function in code section
    uint16_t segment;           // Segment of function
    uint16_t flags;             // Line flags
    uint32_t code_length;       // Length of function
    // REMOVED: reserved field - LineInfoHeader should be 12 bytes, not 16
};
#pragma pack(pop)

// File block header for line info
#pragma pack(push, 1)
struct FileBlockHeader {
    uint32_t file_id;           // Index into file checksum table
    uint32_t num_lines;         // Number of line entries
    uint32_t block_size;        // Size of this block
};
#pragma pack(pop)

// Local variable symbol record
struct LocalVariableSymbol {
    uint16_t length;
    uint16_t kind;          // S_LOCAL
    uint32_t type_index;    // Type index
    uint16_t flags;         // Variable flags
    // Variable name follows (null-terminated string)
};

// Local variable address range
struct LocalVariableAddrRange {
    uint32_t offset_start;
    uint16_t section_start;
    uint16_t length;
    // Gap information follows if needed
};

// Local variable information
struct LocalVariableInfo {
    std::string name;
    uint32_t type_index;
    uint32_t stack_offset;  // Offset from frame pointer
    uint32_t start_offset;  // Code offset where variable becomes valid
    uint32_t end_offset;    // Code offset where variable goes out of scope
    uint16_t flags;
};

// S_COMPILE3 symbol record structure
#pragma pack(push, 1)
struct CompileSymbol3 {
    uint8_t language;           // Language (CV_CFL_CXX = 0x01 for C++)
    uint8_t flags[3];           // Compilation flags (3 bytes)
    uint16_t machine;           // Target machine (CV_CFL_X64 = 0xD0)
    uint16_t frontend_major;    // Frontend version major
    uint16_t frontend_minor;    // Frontend version minor
    uint16_t frontend_build;    // Frontend version build
    uint16_t frontend_qfe;      // Frontend version QFE
    uint16_t backend_major;     // Backend version major
    uint16_t backend_minor;     // Backend version minor
    uint16_t backend_build;     // Backend version build
    uint16_t backend_qfe;       // Backend version QFE
    // Version string follows (null-terminated)
};
#pragma pack(pop)

// CodeView language constants
enum CV_CFL_LANG : uint8_t {
    CV_CFL_C = 0x00,
    CV_CFL_CXX = 0x01,
    CV_CFL_FORTRAN = 0x02,
    CV_CFL_MASM = 0x03,
    CV_CFL_PASCAL = 0x04,
    CV_CFL_BASIC = 0x05,
    CV_CFL_COBOL = 0x06,
    CV_CFL_LINK = 0x07,
    CV_CFL_CVTRES = 0x08,
    CV_CFL_CVTPGD = 0x09
};

// CodeView machine constants
enum CV_CPU_TYPE : uint16_t {
    CV_CFL_8080 = 0x00,
    CV_CFL_8086 = 0x01,
    CV_CFL_80286 = 0x02,
    CV_CFL_80386 = 0x03,
    CV_CFL_80486 = 0x04,
    CV_CFL_PENTIUM = 0x05,
    CV_CFL_PENTIUMII = 0x06,
    CV_CFL_PENTIUMIII = 0x07,
    CV_CFL_MIPS = 0x10,
    CV_CFL_MIPSR4000 = 0x11,
    CV_CFL_MIPS16 = 0x12,
    CV_CFL_MIPS32 = 0x13,
    CV_CFL_MIPS64 = 0x14,
    CV_CFL_MIPSI = 0x15,
    CV_CFL_MIPSII = 0x16,
    CV_CFL_MIPSIII = 0x17,
    CV_CFL_MIPSIV = 0x18,
    CV_CFL_MIPSV = 0x19,
    CV_CFL_M68000 = 0x20,
    CV_CFL_M68010 = 0x21,
    CV_CFL_M68020 = 0x22,
    CV_CFL_M68030 = 0x23,
    CV_CFL_M68040 = 0x24,
    CV_CFL_ALPHA = 0x30,
    CV_CFL_ALPHA21164 = 0x31,
    CV_CFL_ALPHA21164A = 0x32,
    CV_CFL_ALPHA21264 = 0x33,
    CV_CFL_PPC601 = 0x40,
    CV_CFL_PPC603 = 0x41,
    CV_CFL_PPC604 = 0x42,
    CV_CFL_PPC620 = 0x43,
    CV_CFL_PPCFP = 0x44,
    CV_CFL_PPCBE = 0x45,
    CV_CFL_SH3 = 0x50,
    CV_CFL_SH3E = 0x51,
    CV_CFL_SH4 = 0x52,
    CV_CFL_SHMEDIA = 0x53,
    CV_CFL_ARM3 = 0x60,
    CV_CFL_ARM4 = 0x61,
    CV_CFL_ARM4T = 0x62,
    CV_CFL_ARM5 = 0x63,
    CV_CFL_ARM5T = 0x64,
    CV_CFL_ARM6 = 0x65,
    CV_CFL_ARM_XMAC = 0x66,
    CV_CFL_ARM_WMMX = 0x67,
    CV_CFL_ARM7 = 0x68,
    CV_CFL_OMNI = 0x70,
    CV_CFL_IA64 = 0x80,
    CV_CFL_IA64_2 = 0x81,
    CV_CFL_CEE = 0x90,
    CV_CFL_AM33 = 0xA0,
    CV_CFL_M32R = 0xB0,
    CV_CFL_TRICORE = 0xC0,
    CV_CFL_X64 = 0xD0,
    CV_CFL_EBC = 0xE0,
    CV_CFL_THUMB = 0xF0,
    CV_CFL_ARMNT = 0xF4,
    CV_CFL_ARM64 = 0xF6
};

// Function parameter information
struct ParameterInfo {
    std::string name;
    uint32_t type_index;
    uint32_t stack_offset;  // Offset from frame pointer (positive for parameters)
};

// Function information structure (moved to public for access from ObjFileWriter)
struct FunctionInfo {
    std::string name;
    uint32_t code_offset;
    uint32_t code_length;
    uint32_t file_id;
    std::vector<std::pair<uint32_t, uint32_t>> line_offsets; // offset, line
    std::vector<LocalVariableInfo> local_variables;
    std::vector<ParameterInfo> parameters;

    // Debug range information (relative to function start)
    uint32_t debug_start_offset = 0;  // Offset where debugging starts (after prologue)
    uint32_t debug_end_offset = 0;    // Offset where debugging ends (before epilogue)
    uint32_t prologue_size = 0;       // Size of function prologue
    uint32_t epilogue_size = 0;       // Size of function epilogue
};

class DebugInfoBuilder {
public:
    DebugInfoBuilder();

    // Add a source file
    uint32_t addSourceFile(const std::string& filename);

    // Add a function symbol with line information
    void addFunction(const std::string& name, uint32_t code_offset, uint32_t code_length);

    // Add a line mapping for the current function
    void addLineMapping(uint32_t code_offset, uint32_t line_number);

    // Set the current function being processed (for line mappings)
    void setCurrentFunction(const std::string& name, uint32_t file_id);

    // Add a local variable to the current function
    void addLocalVariable(const std::string& name, uint32_t type_index,
                         uint32_t stack_offset, uint32_t start_offset, uint32_t end_offset);

    // Add a function parameter to the current function
    void addFunctionParameter(const std::string& name, uint32_t type_index, uint32_t stack_offset);

    // Update function length for a previously added function
    void updateFunctionLength(const std::string& name, uint32_t code_length);

    // Set debug range information for a function
    void setFunctionDebugRange(const std::string& name, uint32_t prologue_size, uint32_t epilogue_size);

    // Set the text section number for symbol references
    void setTextSectionNumber(uint16_t section_number);

    // Finalize the current function (should be called before generating debug sections)
    void finalizeCurrentFunction();

    // Generate .debug$S section data
    std::vector<uint8_t> generateDebugS();

    // Generate .debug$T section data
    std::vector<uint8_t> generateDebugT();

    // Get function information for dynamic symbol generation
    const std::vector<FunctionInfo>& getFunctions() const { return functions_; }

private:
    std::vector<std::string> source_files_;
    std::unordered_map<std::string, uint32_t> file_name_to_id_;
    std::vector<uint8_t> string_table_;
    std::unordered_map<std::string, uint32_t> string_offsets_;

    std::vector<FunctionInfo> functions_;

    // Current function being processed (for incremental line mapping)
    std::string current_function_name_;
    uint32_t current_function_file_id_;
    std::vector<std::pair<uint32_t, uint32_t>> current_function_lines_;

    // Text section number for symbol references
    uint16_t text_section_number_ = 1; // Default to 1, will be updated by ObjectFileWriter

    // Function ID mapping - maps function names to their LF_FUNC_ID type indices
    std::unordered_map<std::string, uint32_t> function_id_map_;

    // Helper methods
    uint32_t addString(const std::string& str);
    void initializeFunctionIdMap();
    void writeSymbolRecord(std::vector<uint8_t>& data, SymbolKind kind, const std::vector<uint8_t>& record_data);
    void writeSubsection(std::vector<uint8_t>& data, DebugSubsectionKind kind, const std::vector<uint8_t>& subsection_data);
    void alignTo4Bytes(std::vector<uint8_t>& data);
    void writeLittleEndian32(std::vector<uint8_t>& data, uint32_t value);
    void writeLittleEndian16(std::vector<uint8_t>& data, uint16_t value);

    // Generate file checksums subsection
    std::vector<uint8_t> generateFileChecksums();

    // Generate line information subsection (combined - old approach)
    std::vector<uint8_t> generateLineInfo();

    // Generate line information for a single function
    std::vector<uint8_t> generateLineInfoForFunction(const FunctionInfo& func);
};

} // namespace CodeView
