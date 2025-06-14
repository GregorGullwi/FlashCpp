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

// Function parameter information
struct ParameterInfo {
    std::string name;
    uint32_t type_index;
    uint32_t stack_offset;  // Offset from frame pointer (positive for parameters)
};

class DebugInfoBuilder {
public:
    DebugInfoBuilder();

    // Add a source file
    uint32_t addSourceFile(const std::string& filename);

    // Add line number information for a function
    void addLineInfo(const std::string& function_name, uint32_t code_offset,
                     uint32_t code_length, uint32_t file_id,
                     const std::vector<std::pair<uint32_t, uint32_t>>& line_offsets);

    // Add a function symbol with line information
    void addFunction(const std::string& name, uint32_t code_offset, uint32_t code_length);

    // Add a function with detailed line information
    void addFunctionWithLines(const std::string& name, uint32_t code_offset,
                             uint32_t code_length, uint32_t file_id,
                             const std::vector<std::pair<uint32_t, uint32_t>>& line_offsets);

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

    // Set the text section number for symbol references
    void setTextSectionNumber(uint16_t section_number);

    // Finalize the current function (should be called before generating debug sections)
    void finalizeCurrentFunction();

    // Generate .debug$S section data
    std::vector<uint8_t> generateDebugS();

    // Generate .debug$T section data
    std::vector<uint8_t> generateDebugT();
    
private:
    std::vector<std::string> source_files_;
    std::unordered_map<std::string, uint32_t> file_name_to_id_;
    std::vector<uint8_t> string_table_;
    std::unordered_map<std::string, uint32_t> string_offsets_;

    struct FunctionInfo {
        std::string name;
        uint32_t code_offset;
        uint32_t code_length;
        uint32_t file_id;
        std::vector<std::pair<uint32_t, uint32_t>> line_offsets; // offset, line
        std::vector<LocalVariableInfo> local_variables;
        std::vector<ParameterInfo> parameters;
    };

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

    // Generate file checksums subsection
    std::vector<uint8_t> generateFileChecksums();

    // Generate line information subsection
    std::vector<uint8_t> generateLineInfo();

    // Generate line information for a single function
    std::vector<uint8_t> generateLineInfoForFunction(const FunctionInfo& func);
};

} // namespace CodeView
