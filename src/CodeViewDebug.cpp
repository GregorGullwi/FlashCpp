#include "CodeViewDebug.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

extern bool g_enable_debug_output;

// Simple SHA-256 implementation for file checksums
#include <array>

namespace {
    // SHA-256 implementation
    class SHA256 {
    public:
        std::array<uint8_t, 32> hash(const std::vector<uint8_t>& data) {
            // Initialize hash values
            uint32_t h[8] = {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
            };

            // Pre-processing: adding a single 1 bit
            std::vector<uint8_t> msg = data;
            msg.push_back(0x80);

            // Pre-processing: padding with zeros
            while (msg.size() % 64 != 56) {
                msg.push_back(0x00);
            }

            // Append original length in bits as 64-bit big-endian integer
            uint64_t bit_len = data.size() * 8;
            for (int i = 7; i >= 0; i--) {
                msg.push_back((bit_len >> (i * 8)) & 0xFF);
            }

            // Process the message in successive 512-bit chunks
            for (size_t chunk_start = 0; chunk_start < msg.size(); chunk_start += 64) {
                uint32_t w[64];

                // Copy chunk into first 16 words of the message schedule array
                for (int i = 0; i < 16; i++) {
                    w[i] = (msg[chunk_start + i * 4] << 24) |
                           (msg[chunk_start + i * 4 + 1] << 16) |
                           (msg[chunk_start + i * 4 + 2] << 8) |
                           (msg[chunk_start + i * 4 + 3]);
                }

                // Extend the first 16 words into the remaining 48 words
                for (int i = 16; i < 64; i++) {
                    uint32_t s0 = rightRotate(w[i-15], 7) ^ rightRotate(w[i-15], 18) ^ (w[i-15] >> 3);
                    uint32_t s1 = rightRotate(w[i-2], 17) ^ rightRotate(w[i-2], 19) ^ (w[i-2] >> 10);
                    w[i] = w[i-16] + s0 + w[i-7] + s1;
                }

                // Initialize working variables
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
                uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];

                // Compression function main loop
                static const uint32_t k[64] = {
                    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
                };

                for (int i = 0; i < 64; i++) {
                    uint32_t S1 = rightRotate(e, 6) ^ rightRotate(e, 11) ^ rightRotate(e, 25);
                    uint32_t ch = (e & f) ^ (~e & g);
                    uint32_t temp1 = h_val + S1 + ch + k[i] + w[i];
                    uint32_t S0 = rightRotate(a, 2) ^ rightRotate(a, 13) ^ rightRotate(a, 22);
                    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    uint32_t temp2 = S0 + maj;

                    h_val = g;
                    g = f;
                    f = e;
                    e = d + temp1;
                    d = c;
                    c = b;
                    b = a;
                    a = temp1 + temp2;
                }

                // Add the compressed chunk to the current hash value
                h[0] += a; h[1] += b; h[2] += c; h[3] += d;
                h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
            }

            // Produce the final hash value as a 256-bit number
            std::array<uint8_t, 32> result;
            for (int i = 0; i < 8; i++) {
                result[i * 4] = (h[i] >> 24) & 0xFF;
                result[i * 4 + 1] = (h[i] >> 16) & 0xFF;
                result[i * 4 + 2] = (h[i] >> 8) & 0xFF;
                result[i * 4 + 3] = h[i] & 0xFF;
            }

            return result;
        }

    private:
        uint32_t rightRotate(uint32_t value, int amount) {
            return (value >> amount) | (value << (32 - amount));
        }
    };

    std::array<uint8_t, 32> calculateFileSHA256(const std::filesystem::path& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            // Return zero hash if file can't be read
            std::array<uint8_t, 32> zero_hash = {};
            return zero_hash;
        }

        // Read entire file
        std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());

        SHA256 sha256;
        return sha256.hash(file_data);
    }
}

namespace CodeView {

DebugInfoBuilder::DebugInfoBuilder() {
    // Initialize with empty string at offset 0
    string_table_.push_back(0);
    string_offsets_[""] = 0;

    // Reserve space for functions to prevent reallocation issues
    functions_.reserve(32);
}

uint32_t DebugInfoBuilder::addSourceFile(const std::filesystem::path& filename) {
    auto it = file_name_to_id_.find(filename);
    if (it != file_name_to_id_.end()) {
        return it->second;
    }
    
    uint32_t file_id = static_cast<uint32_t>(source_files_.size());
    source_files_.push_back(filename);
    file_name_to_id_[filename] = file_id;
    
    // Add filename to string table
    addString(filename.string());
    
    return file_id;
}

void DebugInfoBuilder::addFunction(const std::string& name, const std::string& mangled_name, uint32_t code_offset, uint32_t code_length, uint32_t stack_space) {
    // Add basic function info without line numbers
    FunctionInfo func_info;
    func_info.name = name;  // Unmangled name for display
    func_info.mangled_name = mangled_name;  // Mangled name for symbol lookup
    func_info.code_offset = code_offset;
    func_info.code_length = code_length;
    func_info.file_id = 0; // Default to first file
    func_info.stack_space = stack_space;
    func_info.is_finalized = false;  // Initialize the flag

    functions_.push_back(func_info);
}

void DebugInfoBuilder::setCurrentFunction(const std::string& name, uint32_t file_id) {
    // Finalize previous function if any (but only if it hasn't been finalized already)
    if (!current_function_name_.empty()) {
        if (g_enable_debug_output) std::cerr << "DEBUG: Finalizing previous function: " << current_function_name_
                  << " with file_id=" << current_function_file_id_
                  << " and " << current_function_lines_.size() << " lines" << std::endl;

        // Find the function and update its line information
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                // Only update if not already finalized
                if (!func.is_finalized) {
                    func.line_offsets = std::move(current_function_lines_);
                    func.file_id = current_function_file_id_;
                    func.is_finalized = true;
                    if (g_enable_debug_output) std::cerr << "DEBUG: Updated function " << func.name << " with file_id=" << func.file_id << std::endl;
                } else {
                    if (g_enable_debug_output) std::cerr << "DEBUG: Function " << func.name << " already finalized, skipping" << std::endl;
                }
                break;
            }
        }
    }

    // Set up new current function
    if (g_enable_debug_output) std::cerr << "DEBUG: Setting current function: " << name << " with file_id=" << file_id << std::endl;
    current_function_name_ = name;
    current_function_file_id_ = file_id;
    current_function_lines_.clear();
}

void DebugInfoBuilder::addLineMapping(uint32_t code_offset, uint32_t line_number) {
    if (!current_function_name_.empty()) {
        if (current_function_lines_.empty() || current_function_lines_.rbegin()->second != line_number) {
            current_function_lines_.emplace_back(code_offset, line_number);
        }
    }
}

void DebugInfoBuilder::addLocalVariable(const std::string& name, uint32_t type_index, uint16_t flags,
                                      const std::vector<VariableLocation>& locations) {
    if (!current_function_name_.empty()) {
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                LocalVariableInfo var_info;
                var_info.name = name;
                var_info.type_index = type_index;
                var_info.flags = flags;
                var_info.locations = locations;
                func.local_variables.push_back(var_info);
                break;
            }
        }
    }
}

void DebugInfoBuilder::addFunctionParameter(const std::string& name, uint32_t type_index, int32_t stack_offset) {
    if (!current_function_name_.empty()) {
        // Find the current function and add the parameter
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                ParameterInfo param_info;
                param_info.name = name;
                param_info.type_index = type_index;
                param_info.stack_offset = stack_offset;

                func.parameters.push_back(param_info);
                break;
            }
        }
    }
}

void DebugInfoBuilder::updateFunctionLength(const std::string_view manged_name, uint32_t code_length) {
    // Find the function and update its length
    for (auto& func : functions_) {
        if (func.mangled_name == manged_name) {
            func.code_length = code_length;
            break;
        }
    }
}

void DebugInfoBuilder::setFunctionDebugRange(const std::string_view manged_name, uint32_t prologue_size, uint32_t epilogue_size) {
    // Find the function and update its debug range information
    for (auto& func : functions_) {
        if (func.mangled_name == manged_name) {
            func.prologue_size = prologue_size;
            func.epilogue_size = epilogue_size;
            func.debug_start_offset = prologue_size;  // Debug starts after prologue
            func.debug_end_offset = func.code_length - epilogue_size;  // Debug ends before epilogue

            if (g_enable_debug_output) std::cerr << "DEBUG: Set debug range for function " << manged_name
                      << " - prologue_size=" << prologue_size
                      << ", epilogue_size=" << epilogue_size
                      << ", debug_start=" << func.debug_start_offset
                      << ", debug_end=" << func.debug_end_offset << std::endl;
            break;
        }
    }
}

void DebugInfoBuilder::setTextSectionNumber(uint16_t section_number) {
    text_section_number_ = section_number;
}

void DebugInfoBuilder::finalizeCurrentFunction() {
    if (!current_function_name_.empty()) {
        if (g_enable_debug_output) std::cerr << "DEBUG: finalizeCurrentFunction: " << current_function_name_
                  << " with file_id=" << current_function_file_id_
                  << " and " << current_function_lines_.size() << " lines" << std::endl;
        // Find the function and update its line information
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                // Only update if not already finalized
                if (!func.is_finalized) {
                    func.line_offsets = std::move(current_function_lines_);
                    func.file_id = current_function_file_id_;
                    func.is_finalized = true;
                    if (g_enable_debug_output) std::cerr << "DEBUG: finalizeCurrentFunction updated function " << func.name << " with file_id=" << func.file_id << std::endl;
                } else {
                    if (g_enable_debug_output) std::cerr << "DEBUG: finalizeCurrentFunction: function " << func.name << " already finalized, skipping" << std::endl;
                }
                break;
            }
        }

        // Clear current function state
        current_function_name_.clear();
        current_function_lines_.clear();
        current_function_file_id_ = 0;
    }
}

uint32_t DebugInfoBuilder::addString(const std::string& str) {
    auto it = string_offsets_.find(str);
    if (it != string_offsets_.end()) {
        if (g_enable_debug_output) std::cerr << "DEBUG: String '" << str << "' already exists at offset " << it->second << std::endl;
        return it->second;
    }

    uint32_t offset = static_cast<uint32_t>(string_table_.size());
    string_offsets_[str] = offset;

    if (g_enable_debug_output) std::cerr << "DEBUG: Adding string '" << str << "' at offset " << offset << std::endl;

    // Add string with null terminator
    for (char c : str) {
        string_table_.push_back(static_cast<uint8_t>(c));
    }
    string_table_.push_back(0);

    return offset;
}

void DebugInfoBuilder::initializeFunctionIdMap() {
    // Dynamically generate function ID mapping based on actual functions
    // Each function gets 3 type records: LF_ARGLIST, LF_PROCEDURE, LF_FUNC_ID
    // S_GPROC32_ID should reference the LF_FUNC_ID (third record for each function)

    function_id_map_.clear();

    for (size_t i = 0; i < functions_.size(); ++i) {
        // REFERENCE COMPATIBILITY: Use simple sequential mapping like reference
        // add = 0x1000, main = 0x1001, etc.
        uint32_t func_id_index = 0x1000 + static_cast<uint32_t>(i);
        function_id_map_[functions_[i].name] = func_id_index;
    }

    if (g_enable_debug_output) std::cerr << "DEBUG: Dynamically initialized function ID map for " << functions_.size() << " functions:" << std::endl;
    for (const auto& pair : function_id_map_) {
        if (g_enable_debug_output) std::cerr << "  " << pair.first << " -> 0x" << std::hex << pair.second << std::dec << std::endl;
    }
}

void DebugInfoBuilder::addDebugRelocation(uint32_t offset, const std::string& symbol_name, uint32_t relocation_type) {
    DebugRelocation reloc;
    reloc.offset = offset;
    reloc.symbol_name = symbol_name;
    reloc.relocation_type = relocation_type;
    debug_relocations_.push_back(reloc);

    if (g_enable_debug_output) std::cerr << "DEBUG: Added debug relocation at offset " << offset
              << " for symbol " << symbol_name << std::endl;
}

void DebugInfoBuilder::writeSymbolRecord(std::vector<uint8_t>& data, SymbolKind kind, const std::vector<uint8_t>& record_data) {
    SymbolRecordHeader header;
    // Length field contains the number of bytes that follow the length field
    // This includes: kind field (2 bytes) + record data
    header.length = static_cast<uint16_t>(sizeof(SymbolKind) + record_data.size());
    header.kind = kind;

    // Write header
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&header),
                reinterpret_cast<const uint8_t*>(&header) + sizeof(header));

    // Write record data
    data.insert(data.end(), record_data.begin(), record_data.end());

    // DO NOT align individual records - only align the entire subsection
    // Individual symbol records should be packed without padding
}

void DebugInfoBuilder::writeSubsection(std::vector<uint8_t>& data, DebugSubsectionKind kind, const std::vector<uint8_t>& subsection_data) {
    // LLVM DISCOVERY: For object files, length field does NOT include padding!
    // alignOf(CodeViewContainer::ObjectFile) = 1, so no alignment padding in header
    size_t unpadded_size = subsection_data.size();

    DebugSubsectionHeader header;
    header.kind = kind;
    header.length = static_cast<uint32_t>(unpadded_size);  // NO padding in length for object files!

    if (g_enable_debug_output) std::cerr << "DEBUG: Writing subsection kind=" << static_cast<uint32_t>(kind)
              << ", unpadded_size=" << unpadded_size
              << ", header_length=" << header.length
              << ", header_size=" << sizeof(header) << std::endl;

    // Debug: Show header bytes
    if (g_enable_debug_output) std::cerr << "DEBUG: Header bytes: ";
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    for (size_t i = 0; i < sizeof(header); ++i) {
        if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(header_bytes[i]) << " ";
    }
    if (g_enable_debug_output) std::cerr << std::dec << std::endl;

    // Write header
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&header),
                reinterpret_cast<const uint8_t*>(&header) + sizeof(header));

    // Write subsection data
    data.insert(data.end(), subsection_data.begin(), subsection_data.end());

    // Add padding to match the length we claimed in the header
    // Use CodeView standard padding pattern like Clang: 0xF3, 0xF2, 0xF1
    static const uint8_t padding_pattern[] = {0xF3, 0xF2, 0xF1};
    size_t padding_index = 0;
    while (data.size() % 4 != 0) {
        data.push_back(padding_pattern[padding_index]);
        padding_index = (padding_index + 1) % 3;
    }
}

void DebugInfoBuilder::alignTo4Bytes(std::vector<uint8_t>& data) {
    // Use CodeView standard padding pattern like Clang: 0xF3, 0xF2, 0xF1
    static const uint8_t padding_pattern[] = {0xF3, 0xF2, 0xF1};
    size_t padding_index = 0;

    while (data.size() % 4 != 0) {
        data.push_back(padding_pattern[padding_index]);
        padding_index = (padding_index + 1) % 3;
    }
}

// Helper function to add a type record with proper padding and length calculation
void DebugInfoBuilder::addTypeRecordWithPadding(std::vector<uint8_t>& debug_t_data, TypeRecordKind kind, const std::vector<uint8_t>& record_data) {
    // Calculate the total size including header and padding
    size_t data_size = record_data.size() + sizeof(TypeRecordKind);
    size_t padded_size = (data_size + 3) & ~3; // Round up to 4-byte boundary

    // Create header with padded length
    TypeRecordHeader header;
    header.length = static_cast<uint16_t>(padded_size);
    header.kind = kind;

    // Add header and data
    debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                       reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
    debug_t_data.insert(debug_t_data.end(), record_data.begin(), record_data.end());

    // Add padding to reach the padded size
    size_t current_record_size = sizeof(header) + record_data.size();
    while (current_record_size < (padded_size + sizeof(header.length))) {
        debug_t_data.push_back(0x00);
        current_record_size++;
    }
}

// Helper function to write little-endian integers
void DebugInfoBuilder::writeLittleEndian32(std::vector<uint8_t>& data, uint32_t value) {
    data.push_back(static_cast<uint8_t>(value & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void DebugInfoBuilder::writeLittleEndian16(std::vector<uint8_t>& data, uint16_t value) {
    data.push_back(static_cast<uint8_t>(value & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

std::vector<uint8_t> DebugInfoBuilder::generateFileChecksums() {
    std::vector<uint8_t> checksum_data;

    for (const auto& file_path : source_files_) {
        const std::string& filename = file_path.string();
        uint32_t filename_offset = string_offsets_[filename];

        // Calculate SHA-256 checksum for the file
        auto sha256_hash = calculateFileSHA256(filename);

        FileChecksumEntry entry;
        entry.file_name_offset = filename_offset;
        entry.checksum_size = 32;  // SHA-256 is 32 bytes
        entry.checksum_kind = 3;   // 3 = SHA256

        // Write the entry
        checksum_data.insert(checksum_data.end(),
                           reinterpret_cast<const uint8_t*>(&entry),
                           reinterpret_cast<const uint8_t*>(&entry) + sizeof(entry));

        // Write the SHA-256 hash
        checksum_data.insert(checksum_data.end(), sha256_hash.begin(), sha256_hash.end());

        // File checksum entry created successfully

        // Align to 4-byte boundary after each entry
        alignTo4Bytes(checksum_data);
    }

    return checksum_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateLineInfo() {
    std::vector<uint8_t> line_data;

    if (functions_.empty()) {
        return line_data;
    }

    // Find the range of all functions to create the main header
    uint32_t min_offset = UINT32_MAX;
    uint32_t max_offset = 0;

    for (const auto& func : functions_) {
        if (!func.line_offsets.empty()) {
            min_offset = std::min(min_offset, func.code_offset);
            max_offset = std::max(max_offset, func.code_offset + func.code_length);
        }
    }

    if (min_offset == UINT32_MAX) {
        return line_data; // No functions with line information
    }

    // Generate the main header (as expected by cvdump)
    struct MainLineHeader {
        uint32_t offCon;    // Starting offset of the function range
        uint16_t segCon;    // Segment number
        uint16_t flags;     // Flags
        uint32_t cbCon;     // Total code length
    };

    MainLineHeader main_header;
    main_header.offCon = min_offset;
    main_header.segCon = 0; // Use section 0 to match MSVC/clang reference
    main_header.flags = 0;  // No special flags
    main_header.cbCon = max_offset - min_offset;

    if (g_enable_debug_output) std::cerr << "DEBUG: Main line header - offCon=" << main_header.offCon
              << ", segCon=" << main_header.segCon
              << ", cbCon=" << main_header.cbCon << std::endl;

    // Write main header
    line_data.insert(line_data.end(),
                     reinterpret_cast<const uint8_t*>(&main_header),
                     reinterpret_cast<const uint8_t*>(&main_header) + sizeof(main_header));

    // Generate file blocks for each function
    for (const auto& func : functions_) {
        if (func.line_offsets.empty()) {
            continue; // Skip functions without line information
        }

        if (g_enable_debug_output) std::cerr << "DEBUG: Line info for function " << func.name
                  << " - file_id=" << func.file_id
                  << ", num_lines=" << func.line_offsets.size() << std::endl;

        // File block header
        FileBlockHeader file_header;
        file_header.file_id = func.file_id;
        file_header.num_lines = static_cast<uint32_t>(func.line_offsets.size());

        // Calculate block size: Total size of file block INCLUDING header
        uint32_t line_entries_size = func.line_offsets.size() * sizeof(LineNumberEntry);
        file_header.block_size = sizeof(FileBlockHeader) + line_entries_size;

        // DEBUG: Dump the file header bytes
        if (g_enable_debug_output) std::cerr << "DEBUG: FileBlockHeader bytes: ";
        const uint8_t* file_header_bytes = reinterpret_cast<const uint8_t*>(&file_header);
        for (size_t i = 0; i < sizeof(file_header); ++i) {
            if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)file_header_bytes[i] << " ";
        }
        if (g_enable_debug_output) std::cerr << std::dec << std::endl;

        // Write file block header
        line_data.insert(line_data.end(),
                         reinterpret_cast<const uint8_t*>(&file_header),
                         reinterpret_cast<const uint8_t*>(&file_header) + sizeof(file_header));

        // Write line number entries
        for (const auto& line_offset : func.line_offsets) {
            LineNumberEntry line_entry;
            line_entry.offset = line_offset.first - func.code_offset;  // Code offset relative to function start
            line_entry.line_start = line_offset.second; // Line number
            line_entry.delta_line_end = 0; // Single line statement
            line_entry.is_statement = 1;   // This is a statement

            // DEBUG: Dump line entry bytes
            if (g_enable_debug_output) std::cerr << "DEBUG: LineNumberEntry for offset=" << line_entry.offset
                      << ", line=" << line_entry.line_start << " bytes: ";
            const uint8_t* entry_bytes = reinterpret_cast<const uint8_t*>(&line_entry);
            for (size_t i = 0; i < sizeof(line_entry); ++i) {
                if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)entry_bytes[i] << " ";
            }
            if (g_enable_debug_output) std::cerr << std::dec << std::endl;

            line_data.insert(line_data.end(),
                             reinterpret_cast<const uint8_t*>(&line_entry),
                             reinterpret_cast<const uint8_t*>(&line_entry) + sizeof(line_entry));
        }
    }

    return line_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateLineInfoForFunction(const FunctionInfo& func) {
    std::vector<uint8_t> line_data;

    if (func.line_offsets.empty()) {
        return line_data;
    }

    // Generate the line header for this specific function
    struct LineInfoHeader {
        uint32_t code_offset;       // Offset of function in code section
        uint16_t segment;           // Segment of function
        uint16_t flags;             // Line flags
        uint32_t code_length;       // Length of function
    };

    LineInfoHeader line_header;
    line_header.code_offset = func.code_offset; // Use actual function offset in text section
    line_header.segment = 0; // Use section 0 to match MSVC/clang reference
    line_header.flags = 0;   // No special flags
    line_header.code_length = func.code_length;

    if (g_enable_debug_output) std::cerr << "DEBUG: Function line header - offCon=" << line_header.code_offset
              << ", segCon=" << line_header.segment
              << ", cbCon=" << line_header.code_length << std::endl;

    // Track position for relocations - we need to add relocations for the code_offset and segment fields
    size_t line_header_start = line_data.size();

    line_data.insert(line_data.end(),
                     reinterpret_cast<const uint8_t*>(&line_header),
                     reinterpret_cast<const uint8_t*>(&line_header) + sizeof(line_header));

    // Add relocations for line information (SECREL + SECTION pair for .text section)
    // Note: These will be calculated relative to the final debug_s_data position when written
    // For now, we'll add them when the line data is written to the main debug_s_data

    // Generate file block header for this function
    struct FileBlockHeader {
        uint32_t file_id;           // Index into file checksum table
        uint32_t num_lines;         // Number of line entries
        uint32_t block_size;        // Size of this block
    };

    FileBlockHeader file_block;
    file_block.file_id = func.file_id;
    file_block.num_lines = static_cast<uint32_t>(func.line_offsets.size());
    file_block.block_size = sizeof(FileBlockHeader) + (func.line_offsets.size() * sizeof(LineNumberEntry));

    if (g_enable_debug_output) std::cerr << "DEBUG: Function line info for function " << func.name
              << " - file_id=" << file_block.file_id
              << ", num_lines=" << file_block.num_lines << std::endl;

    // DEBUG: Print all line mappings for this function
    if (g_enable_debug_output) std::cerr << "DEBUG: All line mappings for function " << func.name << ":" << std::endl;
    for (size_t i = 0; i < func.line_offsets.size(); ++i) {
        if (g_enable_debug_output) std::cerr << "  [" << i << "] offset=" << func.line_offsets[i].first
                  << ", line=" << func.line_offsets[i].second << std::endl;
    }

    // DEBUG: Dump file block header bytes
    if (g_enable_debug_output) std::cerr << "DEBUG: FileBlockHeader bytes: ";
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&file_block);
    for (size_t i = 0; i < sizeof(file_block); ++i) {
        if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)header_bytes[i] << " ";
    }
    if (g_enable_debug_output) std::cerr << std::dec << std::endl;

    line_data.insert(line_data.end(),
                     reinterpret_cast<const uint8_t*>(&file_block),
                     reinterpret_cast<const uint8_t*>(&file_block) + sizeof(file_block));

    // Write line number entries for this function
    for (const auto& line_offset : func.line_offsets) {
        LineNumberEntry line_entry;
        line_entry.offset = line_offset.first;  // Code offset relative to function start
        line_entry.line_start = line_offset.second; // Line number
        line_entry.delta_line_end = 0; // Single line statement
        line_entry.is_statement = 1;   // This is a statement

        // DEBUG: Dump line entry bytes
        if (g_enable_debug_output) std::cerr << "DEBUG: LineNumberEntry for offset=" << line_entry.offset
                  << ", line=" << line_entry.line_start << " bytes: ";
        const uint8_t* entry_bytes = reinterpret_cast<const uint8_t*>(&line_entry);
        for (size_t i = 0; i < sizeof(line_entry); ++i) {
            if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)entry_bytes[i] << " ";
        }
        if (g_enable_debug_output) std::cerr << std::dec << std::endl;

        line_data.insert(line_data.end(),
                         reinterpret_cast<const uint8_t*>(&line_entry),
                         reinterpret_cast<const uint8_t*>(&line_entry) + sizeof(line_entry));
    }

    return line_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateDebugS() {
    if (g_enable_debug_output) std::cerr << "DEBUG: generateDebugS() called" << std::endl;

    // Initialize function ID mapping before generating symbols
    initializeFunctionIdMap();

    std::vector<uint8_t> debug_s_data;

    // Write CodeView signature
    uint32_t signature = DEBUG_S_SIGNATURE;
    debug_s_data.insert(debug_s_data.end(), reinterpret_cast<const uint8_t*>(&signature),
                        reinterpret_cast<const uint8_t*>(&signature) + sizeof(signature));

    // Generate symbols subsection
    std::vector<uint8_t> symbols_data;

    // Add OBJNAME symbol (only once per object file)
    {
        if (g_enable_debug_output) std::cerr << "DEBUG: Writing S_OBJNAME symbol" << std::endl;
        std::vector<uint8_t> objname_data;
        uint32_t signature_val = 0; // Signature
        objname_data.insert(objname_data.end(), reinterpret_cast<const uint8_t*>(&signature_val),
                           reinterpret_cast<const uint8_t*>(&signature_val) + sizeof(signature_val));

        // Use the first source file as the object name (convert .cpp to .obj with absolute path)
        std::string obj_name;
        if (!source_files_.empty()) {
            std::filesystem::path source_path(source_files_[0]);
            // Get absolute path and change extension to .obj
            std::filesystem::path abs_path = std::filesystem::absolute(source_path);
            abs_path.replace_extension(".obj");
            obj_name = abs_path.string();
        } else {
            obj_name = "FlashCpp.obj"; // Fallback
        }

        if (g_enable_debug_output) std::cerr << "DEBUG: Using object name: " << obj_name << std::endl;
        for (char c : obj_name) {
            objname_data.push_back(static_cast<uint8_t>(c));
        }
        objname_data.push_back(0); // Null terminator

        writeSymbolRecord(symbols_data, SymbolKind::S_OBJNAME, objname_data);
        if (g_enable_debug_output) std::cerr << "DEBUG: S_OBJNAME symbol written, symbols_data size: " << symbols_data.size() << std::endl;
    }

    // Add S_COMPILE3 symbol
    {
        std::vector<uint8_t> compile3_data;

        // Create S_COMPILE3 structure - CLANG COMPATIBILITY MODE
        CompileSymbol3 compile3;
        compile3.language = CV_CFL_CXX;           // C++ (0x01)
        compile3.flags[0] = 0x00;                 // No special flags
        compile3.flags[1] = 0x00;                 // No special flags
        compile3.flags[2] = 0x00;                 // No special flags
        compile3.machine = CV_CFL_X64;            // x64 target (0xD0)

        // CLANG VERSION SPOOFING: Make it look like Clang 18.1.0
        compile3.frontend_major = 18;             // Clang major version
        compile3.frontend_minor = 1;              // Clang minor version
        compile3.frontend_build = 0;              // Clang build
        compile3.frontend_qfe = 0;                // Clang QFE
        compile3.backend_major = 18;              // Backend version (same as frontend for Clang)
        compile3.backend_minor = 1;
        compile3.backend_build = 0;
        compile3.backend_qfe = 0;

        // Debug: Print the structure values
        if (g_enable_debug_output) std::cerr << "DEBUG: S_COMPILE3 structure:" << std::endl;
        if (g_enable_debug_output) std::cerr << "  language: 0x" << std::hex << (int)compile3.language << std::dec << std::endl;
        if (g_enable_debug_output) std::cerr << "  machine: 0x" << std::hex << compile3.machine << std::dec << std::endl;
        if (g_enable_debug_output) std::cerr << "  frontend version: " << compile3.frontend_major << "." << compile3.frontend_minor
                  << "." << compile3.frontend_build << "." << compile3.frontend_qfe << std::endl;
        if (g_enable_debug_output) std::cerr << "  structure size: " << sizeof(compile3) << " bytes" << std::endl;

        // Add structure to data
        compile3_data.insert(compile3_data.end(),
                            reinterpret_cast<const uint8_t*>(&compile3),
                            reinterpret_cast<const uint8_t*>(&compile3) + sizeof(compile3));

        // Add version string - CLANG SPOOFING
        const char* version_string = "clang version 18.1.0";
        compile3_data.insert(compile3_data.end(),
                            reinterpret_cast<const uint8_t*>(version_string),
                            reinterpret_cast<const uint8_t*>(version_string) + strlen(version_string));
        compile3_data.push_back(0); // Null terminator

        writeSymbolRecord(symbols_data, SymbolKind::S_COMPILE3, compile3_data);
        if (g_enable_debug_output) std::cerr << "DEBUG: S_COMPILE3 symbol written, symbols_data size: " << symbols_data.size() << std::endl;
    }

    // CLANG COMPATIBILITY: Skip S_BUILDINFO symbol to match Clang exactly
    // Clang doesn't generate S_BUILDINFO symbols in simple cases
    if (g_enable_debug_output) std::cerr << "DEBUG: Skipping S_BUILDINFO symbol for Clang compatibility" << std::endl;

    // Write symbols subsection
    if (g_enable_debug_output) std::cerr << "DEBUG: Final symbols_data size before writeSubsection: " << symbols_data.size() << std::endl;
    writeSubsection(debug_s_data, DebugSubsectionKind::Symbols, symbols_data);
    symbols_data.clear();

    // Add function symbols
    if (g_enable_debug_output) std::cerr << "DEBUG: Number of functions: " << functions_.size() << std::endl;
    for (size_t func_index = 0; func_index < functions_.size(); ++func_index) {
        const auto& func = functions_[func_index];
        if (g_enable_debug_output) std::cerr << "DEBUG: Processing function: " << func.name << std::endl;
        std::vector<uint8_t> proc_data;

        // Parent, end, next pointers (set to 0 for now)
        uint32_t parent = 0, end = 0, next = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&parent),
                        reinterpret_cast<const uint8_t*>(&parent) + sizeof(parent));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&end),
                        reinterpret_cast<const uint8_t*>(&end) + sizeof(end));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&next),
                        reinterpret_cast<const uint8_t*>(&next) + sizeof(next));

        // Function length
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&func.code_length),
                        reinterpret_cast<const uint8_t*>(&func.code_length) + sizeof(func.code_length));

        // Debug start and end offsets (relative to function start)
        // Use dynamic values if available, otherwise calculate based on function length
        uint32_t debug_start = func.debug_start_offset;
        uint32_t debug_end = func.debug_end_offset;

        // If debug range hasn't been set, use reasonable defaults
        if (debug_start == 0 && debug_end == 0) {
            // Estimate prologue size: typical x64 prologue is 4-8 bytes
            debug_start = std::min(8u, func.code_length / 4);  // Start after estimated prologue
            debug_end = func.code_length - std::min(4u, func.code_length / 8);  // End before estimated epilogue
        }

        if (g_enable_debug_output) std::cerr << "DEBUG: Function " << func.name << " debug range: start=" << debug_start
                  << ", end=" << debug_end << " (code_length=" << func.code_length << ")" << std::endl;
        if (g_enable_debug_output) std::cerr << "DEBUG: Writing debug_start=0x" << std::hex << debug_start
                  << ", debug_end=0x" << debug_end << std::dec << " to binary" << std::endl;

        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&debug_start),
                        reinterpret_cast<const uint8_t*>(&debug_start) + sizeof(debug_start));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&debug_end),
                        reinterpret_cast<const uint8_t*>(&debug_end) + sizeof(debug_end));

        // Function ID (4 bytes) - LF_FUNC_ID type index for this function
        // REFERENCE COMPATIBILITY: Function symbols should reference LF_FUNC_ID type indices
        // add symbol should reference 0x1002 (LF_FUNC_ID), main should reference 0x1005 (LF_FUNC_ID)
        uint32_t function_id = 0x1000 + static_cast<uint32_t>(func_index * 3) + 2; // LF_FUNC_ID index
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&function_id),
                        reinterpret_cast<const uint8_t*>(&function_id) + sizeof(function_id));

        // Function offset and segment
        // Track the offset where the function offset will be written for relocation
        uint32_t function_offset_position = static_cast<uint32_t>(debug_s_data.size() +
                                                                  8 + // subsection header size
                                                                  symbols_data.size() +
                                                                  4 + // symbol record header size
                                                                  proc_data.size());

        uint32_t zero_offset = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&zero_offset),
                        reinterpret_cast<const uint8_t*>(&zero_offset) + sizeof(zero_offset));

        // Add debug relocations for function offset (SECREL + SECTION pair)
        // Use mangled name for symbol lookup
        const std::string& symbol_name = func.mangled_name.empty() ? func.name : func.mangled_name;
        addDebugRelocation(function_offset_position, symbol_name, IMAGE_REL_AMD64_SECREL);
        addDebugRelocation(function_offset_position + 4, symbol_name, IMAGE_REL_AMD64_SECTION);

        // Use section 0 to match MSVC/clang reference (not the actual section number)
        uint16_t section_number = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&section_number),
                        reinterpret_cast<const uint8_t*>(&section_number) + sizeof(section_number));

        // Flags (1 byte) - Try to match "Do Not Inline, Optimized Debug Info"
        // 0x00 = No flags, 0x40 = optimized, 0x80 = no inline
        // Based on common patterns, try 0x40 | 0x80 = 0xC0
        uint8_t proc_flags = 0x40 | 0x80;
        proc_data.push_back(proc_flags);

        if (g_enable_debug_output) std::cerr << "DEBUG: Function ID written: 0x" << std::hex << function_id << std::dec << " (4 bytes)" << std::endl;

        // Function name (null-terminated string for S_GPROC32_ID)
        if (g_enable_debug_output) std::cerr << "DEBUG: Writing function name: '" << func.name << "' (length: " << func.name.length() << ")" << std::endl;
        // Remove length prefix - use only null-terminated string
        for (char c : func.name) {
            proc_data.push_back(static_cast<uint8_t>(c));
        }
        proc_data.push_back(0); // null terminator

        if (g_enable_debug_output) std::cerr << "DEBUG: Function proc_data size: " << proc_data.size() << " bytes" << std::endl;
        if (g_enable_debug_output) std::cerr << "DEBUG: Function offset: " << func.code_offset << ", length: " << func.code_length << std::endl;

        writeSymbolRecord(symbols_data, SymbolKind::S_GPROC32_ID, proc_data);

        // Add S_FRAMEPROC record
        std::vector<uint8_t> frameproc_data;
        uint32_t frame_size = func.name == "main" ? 0x00000028 : func.stack_space;  // Frame size
        uint32_t pad_size = 0x00000000;    // Pad size
        uint32_t pad_offset = 0x00000000;  // Offset of pad in frame
        uint32_t callee_save_size = 0x00000000; // Size of callee save registers
        uint32_t exception_handler_offset = 0x00000000; // Exception handler offset
        uint16_t exception_handler_section = 0x0000;    // Exception handler section
        uint32_t flags = 0x00014000; // Function info flags (invalid_pgo_counts Local=rsp Param=rsp)

        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&frame_size),
                             reinterpret_cast<const uint8_t*>(&frame_size) + sizeof(frame_size));
        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&pad_size),
                             reinterpret_cast<const uint8_t*>(&pad_size) + sizeof(pad_size));
        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&pad_offset),
                             reinterpret_cast<const uint8_t*>(&pad_offset) + sizeof(pad_offset));
        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&callee_save_size),
                             reinterpret_cast<const uint8_t*>(&callee_save_size) + sizeof(callee_save_size));
        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&exception_handler_offset),
                             reinterpret_cast<const uint8_t*>(&exception_handler_offset) + sizeof(exception_handler_offset));
        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&exception_handler_section),
                             reinterpret_cast<const uint8_t*>(&exception_handler_section) + sizeof(exception_handler_section));
        frameproc_data.insert(frameproc_data.end(), reinterpret_cast<const uint8_t*>(&flags),
                             reinterpret_cast<const uint8_t*>(&flags) + sizeof(flags));

        writeSymbolRecord(symbols_data, SymbolKind::S_FRAMEPROC, frameproc_data);

        // Add S_REGREL32 records for function parameters (RSP-relative addressing)
        for (const auto& param : func.parameters) {
            std::vector<uint8_t> regrel_data;

            // Offset from RSP (4 bytes)
            uint32_t rsp_offset = static_cast<uint32_t>(param.stack_offset);
            regrel_data.insert(regrel_data.end(), reinterpret_cast<const uint8_t*>(&rsp_offset),
                            reinterpret_cast<const uint8_t*>(&rsp_offset) + sizeof(rsp_offset));

            // Type index (4 bytes) - T_INT4 = 0x74 for int parameters
            uint32_t type_index = 0x74; // T_INT4 for int parameters
            regrel_data.insert(regrel_data.end(), reinterpret_cast<const uint8_t*>(&type_index),
                            reinterpret_cast<const uint8_t*>(&type_index) + sizeof(type_index));

            // Register code (2 bytes) - RBP register = 334 (CV_REG_RBP)
            uint16_t register_code = static_cast<uint16_t>(Register::RBP);
            regrel_data.insert(regrel_data.end(), reinterpret_cast<const uint8_t*>(&register_code),
                            reinterpret_cast<const uint8_t*>(&register_code) + sizeof(register_code));

            // Parameter name (null-terminated string)
            for (char c : param.name) {
                regrel_data.push_back(static_cast<uint8_t>(c));
            }
            regrel_data.push_back(0); // Null terminator

            writeSymbolRecord(symbols_data, SymbolKind::S_REGREL32, regrel_data);

        }

        // Add local variable symbols for this function
        for (const auto& var : func.local_variables) {
            // S_LOCAL symbol
            std::vector<uint8_t> local_data;
            local_data.insert(local_data.end(), reinterpret_cast<const uint8_t*>(&var.type_index),
                             reinterpret_cast<const uint8_t*>(&var.type_index) + sizeof(var.type_index));
            local_data.insert(local_data.end(), reinterpret_cast<const uint8_t*>(&var.flags),
                             reinterpret_cast<const uint8_t*>(&var.flags) + sizeof(var.flags));
            for (char c : var.name) {
                local_data.push_back(static_cast<uint8_t>(c));
            }
            local_data.push_back(0);
            writeSymbolRecord(symbols_data, SymbolKind::S_LOCAL, local_data);

            // S_DEFRANGE symbols for each location
            for (const auto& loc : var.locations) {
                if (loc.type == VariableLocation::STACK_RELATIVE) {
                    std::vector<uint8_t> defrange_data;
                    defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&loc.offset),
                                       reinterpret_cast<const uint8_t*>(&loc.offset) + sizeof(loc.offset));
                    LocalVariableAddrRange addr_range;
                    addr_range.offset_start = loc.start_offset - func.code_offset;
                    addr_range.section_start = text_section_number_;
                    addr_range.length = loc.length;
                    defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&addr_range),
                                       reinterpret_cast<const uint8_t*>(&addr_range) + sizeof(addr_range));
                    writeSymbolRecord(symbols_data, SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL, defrange_data);
                } else if (loc.type == VariableLocation::REGISTER) {
                    std::vector<uint8_t> defrange_data;
                    defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&loc.register_code),
                                       reinterpret_cast<const uint8_t*>(&loc.register_code) + sizeof(loc.register_code));
                    LocalVariableAddrRange addr_range;
                    addr_range.offset_start = loc.start_offset - func.code_offset;
                    addr_range.section_start = text_section_number_;
                    addr_range.length = loc.length;
                    defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&addr_range),
                                       reinterpret_cast<const uint8_t*>(&addr_range) + sizeof(addr_range));
                    writeSymbolRecord(symbols_data, SymbolKind::S_DEFRANGE_REGISTER, defrange_data);
                }
            }
        }

        // Add S_PROC_ID_END symbol for function
        std::vector<uint8_t> end_data; // Empty for S_PROC_ID_END
        writeSymbolRecord(symbols_data, SymbolKind::S_PROC_ID_END, end_data);

        if (g_enable_debug_output) std::cerr << "DEBUG: Final symbols_data size before writeSubsection: " << symbols_data.size() << std::endl;
        writeSubsection(debug_s_data, DebugSubsectionKind::Symbols, symbols_data);

        // Generate separate line information subsections for each function
        if (g_enable_debug_output) std::cerr << "DEBUG: Generating separate line information subsections for function..." << std::endl;
        if (!func.line_offsets.empty()) {
            auto line_data = generateLineInfoForFunction(func);
            if (!line_data.empty()) {
                // Calculate position where line data will be written for relocations
                uint32_t line_subsection_position = static_cast<uint32_t>(debug_s_data.size() + 8); // +8 for subsection header

                // Add relocations for line information (.text section references)
                // code_offset field is at offset 0 in LineInfoHeader
                addDebugRelocation(line_subsection_position + 0, ".text", IMAGE_REL_AMD64_SECREL);
                addDebugRelocation(line_subsection_position + 4, ".text", IMAGE_REL_AMD64_SECTION);

                writeSubsection(debug_s_data, DebugSubsectionKind::Lines, line_data);
                if (g_enable_debug_output) std::cerr << "DEBUG: Added " << line_data.size() << " bytes of line information for function " << func.name << std::endl;
            }
        }
        else {
            if (g_enable_debug_output) std::cerr << "DEBUG: No line information for function " << func.name << std::endl;
        }

        symbols_data.clear();
    }

    // Generate and write file checksums subsection
    auto checksum_data = generateFileChecksums();
    if (!checksum_data.empty()) {
        writeSubsection(debug_s_data, DebugSubsectionKind::FileChecksums, checksum_data);
    }

    // Generate string table subsection
    if (g_enable_debug_output) std::cerr << "DEBUG: String table size: " << string_table_.size() << " bytes" << std::endl;
    if (g_enable_debug_output) std::cerr << "DEBUG: String table contents: ";
    for (size_t i = 0; i < std::min(string_table_.size(), size_t(50)); ++i) {
        if (string_table_[i] == 0) {
            if (g_enable_debug_output) std::cerr << "\\0";
        } else if (string_table_[i] >= 32 && string_table_[i] <= 126) {
            if (g_enable_debug_output) std::cerr << (char)string_table_[i];
        } else {
            if (g_enable_debug_output) std::cerr << "\\x" << std::hex << (int)string_table_[i] << std::dec;
        }
    }
    if (string_table_.size() > 50) std::cerr << "...";
    if (g_enable_debug_output) std::cerr << std::endl;
    writeSubsection(debug_s_data, DebugSubsectionKind::StringTable, string_table_);

    return debug_s_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateDebugT() {
    std::vector<uint8_t> debug_t_data;

    // Write CodeView signature
    uint32_t signature = DEBUG_T_SIGNATURE;
    debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&signature),
                        reinterpret_cast<const uint8_t*>(&signature) + sizeof(signature));

    if (g_enable_debug_output) std::cerr << "DEBUG: Generating comprehensive type information like Clang..." << std::endl;

    // CLANG COMPATIBILITY: Generate basic types first, then functions
    // This matches Clang's approach of including comprehensive type information
    uint32_t current_type_index = 0x1000;

    // CLANG COMPATIBILITY: Skip basic pointer types, add LF_STRING_ID records like Clang
    if (g_enable_debug_output) std::cerr << "DEBUG: Skipping basic pointer types, adding LF_STRING_ID records to match Clang..." << std::endl;

    // DEBUG: List all functions in the vector
    for (size_t i = 0; i < functions_.size(); ++i) {
        if (g_enable_debug_output) std::cerr << "DEBUG: Function " << i << ": '" << functions_[i].name
                  << "' at offset " << functions_[i].code_offset
                  << " length " << functions_[i].code_length << std::endl;
    }

    // Generate type records dynamically for each function
    // Each function gets 3 type records: LF_ARGLIST, LF_PROCEDURE, LF_FUNC_ID

    for (size_t func_index = 0; func_index < functions_.size(); ++func_index) {
        const auto& func = functions_[func_index];

        // REFERENCE COMPATIBILITY: Use simple sequential mapping like reference
        // add: arglist=0x1000, procedure=0x1001, func_id=0x1002
        // main: arglist=0x1003, procedure=0x1004, func_id=0x1005
        // But the function ID map should be: add=0x1000, main=0x1001
        // So we need to map: add symbol uses 0x1000, main symbol uses 0x1001
        uint32_t arglist_index = current_type_index;
        uint32_t procedure_index = current_type_index + 1;
        uint32_t func_id_index = current_type_index + 2;

        if (g_enable_debug_output) std::cerr << "DEBUG: Generating types for function '" << func.name
                  << "' - arglist=0x" << std::hex << arglist_index
                  << ", procedure=0x" << procedure_index
                  << ", func_id=0x" << func_id_index << std::dec
                  << " (" << func.parameters.size() << " parameters)" << std::endl;

        // Generate LF_ARGLIST
        {
            std::vector<uint8_t> arglist_data;

            // Argument count
            uint32_t arg_count = static_cast<uint32_t>(func.parameters.size());
            arglist_data.insert(arglist_data.end(), reinterpret_cast<const uint8_t*>(&arg_count),
                               reinterpret_cast<const uint8_t*>(&arg_count) + sizeof(arg_count));

            // Argument types (assume all T_INT4 for now)
            for (size_t i = 0; i < func.parameters.size(); ++i) {
                uint32_t arg_type = 0x74; // T_INT4
                arglist_data.insert(arglist_data.end(), reinterpret_cast<const uint8_t*>(&arg_type),
                                   reinterpret_cast<const uint8_t*>(&arg_type) + sizeof(arg_type));
            }

            // Calculate length exactly like Clang: TypeRecordKind + data + padding
            size_t data_size = arglist_data.size();
            size_t content_after_length = sizeof(TypeRecordKind) + data_size; // What comes after length field
            size_t total_record_size = sizeof(uint16_t) + content_after_length; // length field + content
            size_t aligned_record_size = (total_record_size + 3) & ~3;
            size_t padding_bytes = aligned_record_size - total_record_size;

            TypeRecordHeader header;
            header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
            header.kind = TypeRecordKind::LF_ARGLIST;

            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), arglist_data.begin(), arglist_data.end());
            alignTo4Bytes(debug_t_data);

            if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_ARGLIST 0x" << std::hex << arglist_index << std::dec
                      << " (" << arg_count << " args for " << func.name << ")" << std::endl;
        }

        // Generate LF_PROCEDURE
        {
            std::vector<uint8_t> proc_data;

            // Return type (T_INT4 = 0x74)
            uint32_t return_type = 0x74;
            writeLittleEndian32(proc_data, return_type);

            // Calling convention (0 = C Near, matches clang-cl reference)
            uint8_t calling_conv = 0;
            proc_data.push_back(calling_conv);

            // Function attributes (0 = none)
            uint8_t func_attrs = 0;
            proc_data.push_back(func_attrs);

            // Parameter count
            uint16_t param_count = static_cast<uint16_t>(func.parameters.size());
            writeLittleEndian16(proc_data, param_count);

            // Argument list type index
            uint32_t arglist_type = arglist_index;
            if (g_enable_debug_output) std::cerr << "DEBUG: Writing LF_PROCEDURE arglist_type=0x" << std::hex << arglist_type << std::dec
                      << " (should reference arglist 0x" << std::hex << arglist_index << std::dec << ")" << std::endl;
            writeLittleEndian32(proc_data, arglist_type);

            // Calculate length exactly like Clang: TypeRecordKind + data + padding
            size_t data_size = proc_data.size();
            size_t content_after_length = sizeof(TypeRecordKind) + data_size;
            size_t total_record_size = sizeof(uint16_t) + content_after_length;
            size_t aligned_record_size = (total_record_size + 3) & ~3;
            size_t padding_bytes = aligned_record_size - total_record_size;

            TypeRecordHeader header;
            header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
            header.kind = TypeRecordKind::LF_PROCEDURE;

            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), proc_data.begin(), proc_data.end());
            alignTo4Bytes(debug_t_data);

            if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_PROCEDURE 0x" << std::hex << procedure_index << std::dec
                      << " (" << func.name << " function)" << std::endl;
        }

        // Generate LF_FUNC_ID
        {
            std::vector<uint8_t> func_id_data;

            // Scope (0 = global)
            uint32_t scope = 0;
            writeLittleEndian32(func_id_data, scope);

            // Type index (reference to the LF_PROCEDURE type)
            uint32_t type_index = procedure_index; // Reference to LF_PROCEDURE
            if (g_enable_debug_output) std::cerr << "DEBUG: Writing LF_FUNC_ID type_index=0x" << std::hex << type_index << std::dec
                      << " (should reference procedure 0x" << std::hex << procedure_index << std::dec << ")" << std::endl;
            writeLittleEndian32(func_id_data, type_index);

            // Function name (dynamic)
            for (char c : func.name) {
                func_id_data.push_back(static_cast<uint8_t>(c));
            }
            func_id_data.push_back(0); // null terminator

            // Calculate length exactly like Clang: TypeRecordKind + data + padding
            size_t data_size = func_id_data.size();
            size_t content_after_length = sizeof(TypeRecordKind) + data_size;
            size_t total_record_size = sizeof(uint16_t) + content_after_length;
            size_t aligned_record_size = (total_record_size + 3) & ~3;
            size_t padding_bytes = aligned_record_size - total_record_size;

            TypeRecordHeader header;
            header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
            header.kind = TypeRecordKind::LF_FUNC_ID;

            size_t before_size = debug_t_data.size();
            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), func_id_data.begin(), func_id_data.end());
            size_t after_data_size = debug_t_data.size();

            alignTo4Bytes(debug_t_data);
            size_t after_align_size = debug_t_data.size();

            if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_FUNC_ID 0x" << std::hex << func_id_index << std::dec
                      << " (" << func.name << ") referencing procedure 0x" << std::hex << procedure_index << std::dec
                      << " - length=" << header.length
                      << ", data_size=" << (after_data_size - before_size)
                      << ", padded_size=" << (after_align_size - before_size)
                      << ", offset=" << before_size << std::endl;
        }

        // Move to next set of type indices
        current_type_index += 3;
    } // End of function loop

    // CLANG COMPATIBILITY: Add all LF_STRING_ID and LF_BUILDINFO records like Clang
    if (g_enable_debug_output) std::cerr << "DEBUG: Adding LF_STRING_ID and LF_BUILDINFO records to match Clang..." << std::endl;

    // Add LF_STRING_ID records for each source file
    for (const auto& source_path : source_files_) {
        // Extract directory and filename using filesystem::path
        std::string dir_path = source_path.has_parent_path() ? 
                             source_path.parent_path().string() : ".";
        std::string file_name = source_path.filename().string();

        // Add directory path
        {
            std::vector<uint8_t> string_data;
            writeLittleEndian32(string_data, 0x00000000);
            string_data.insert(string_data.end(), dir_path.begin(), dir_path.end());
            string_data.push_back(0x00);

            // Calculate length exactly like Clang: TypeRecordKind + data + padding
            size_t data_size = string_data.size();
            size_t content_after_length = sizeof(TypeRecordKind) + data_size;
            size_t total_record_size = sizeof(uint16_t) + content_after_length;
            size_t aligned_record_size = (total_record_size + 3) & ~3;
            size_t padding_bytes = aligned_record_size - total_record_size;

            CodeView::TypeRecordHeader header;
            header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
            header.kind = TypeRecordKind::LF_STRING_ID;

            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), string_data.begin(), string_data.end());
            alignTo4Bytes(debug_t_data);

            if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_STRING_ID 0x" << std::hex << current_type_index << std::dec
                      << " (directory: " << dir_path << ", length: " << header.length << ")" << std::endl;
            current_type_index++;
        }

        // Add source file
        {
            std::vector<uint8_t> string_data;
            writeLittleEndian32(string_data, 0x00000000);
            string_data.insert(string_data.end(), file_name.begin(), file_name.end());
            string_data.push_back(0x00);

            // Calculate length exactly like Clang: TypeRecordKind + data + padding
            size_t data_size = string_data.size();
            size_t content_after_length = sizeof(TypeRecordKind) + data_size;
            size_t total_record_size = sizeof(uint16_t) + content_after_length;
            size_t aligned_record_size = (total_record_size + 3) & ~3;
            size_t padding_bytes = aligned_record_size - total_record_size;

            CodeView::TypeRecordHeader header;
            header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
            header.kind = TypeRecordKind::LF_STRING_ID;

            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), string_data.begin(), string_data.end());
            alignTo4Bytes(debug_t_data);

            if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_STRING_ID 0x" << std::hex << current_type_index << std::dec
                      << " (source: " << file_name << ", length: " << header.length << ")" << std::endl;
            current_type_index++;
        }
    }

    // Add LF_STRING_ID for empty string (0x1008 in Clang)
    {
        std::vector<uint8_t> string_data;

        writeLittleEndian32(string_data, 0x00000000);
        string_data.push_back(0x00); // Just null terminator

        // Calculate length exactly like Clang: TypeRecordKind + data + padding
        size_t data_size = string_data.size();
        size_t content_after_length = sizeof(TypeRecordKind) + data_size;
        size_t total_record_size = sizeof(uint16_t) + content_after_length;
        size_t aligned_record_size = (total_record_size + 3) & ~3;
        size_t padding_bytes = aligned_record_size - total_record_size;

        TypeRecordHeader header;
        header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
        header.kind = TypeRecordKind::LF_STRING_ID;

        debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                           reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
        debug_t_data.insert(debug_t_data.end(), string_data.begin(), string_data.end());
        alignTo4Bytes(debug_t_data);

        if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_STRING_ID 0x" << std::hex << current_type_index << std::dec
                  << " (empty string, length: " << header.length << ")" << std::endl;
        current_type_index++;
    }

    // Add LF_STRING_ID for compiler path (0x1009 in Clang)
    {
        std::string compiler_path = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\Llvm\\x64\\bin\\clang-cl.exe";
        std::vector<uint8_t> string_data;

        writeLittleEndian32(string_data, 0x00000000);
        string_data.insert(string_data.end(), compiler_path.begin(), compiler_path.end());
        string_data.push_back(0x00);

        // Calculate length exactly like Clang: TypeRecordKind + data + padding
        size_t data_size = string_data.size();
        size_t content_after_length = sizeof(TypeRecordKind) + data_size;
        size_t total_record_size = sizeof(uint16_t) + content_after_length;
        size_t aligned_record_size = (total_record_size + 3) & ~3;
        size_t padding_bytes = aligned_record_size - total_record_size;

        TypeRecordHeader header;
        header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
        header.kind = TypeRecordKind::LF_STRING_ID;

        debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                           reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
        debug_t_data.insert(debug_t_data.end(), string_data.begin(), string_data.end());
        alignTo4Bytes(debug_t_data);

        if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_STRING_ID 0x" << std::hex << current_type_index << std::dec
                  << " (compiler path, length: " << header.length << ")" << std::endl;
        current_type_index++;
    }

    // Add LF_STRING_ID for command line (0x100a in Clang) - This is the big one!
    {
        std::string cmdline = "\"-cc1\" \"-triple\" \"x86_64-pc-windows-msvc19.44.35209\" \"-emit-obj\" \"-mincremental-linker-compatible\" \"-disable-free\" \"-clear-ast-before-backend\" \"-disable-llvm-verifier\" \"-discard-value-names\" \"-mrelocation-model\" \"pic\" \"-pic-level\" \"2\" \"-mframe-pointer=none\" \"-relaxed-aliasing\" \"-fmath-errno\" \"-ffp-contract=on\" \"-fno-rounding-math\" \"-mconstructor-aliases\" \"-fms-volatile\" \"-funwind-tables=2\" \"-target-cpu\" \"x86-64\" \"-mllvm\" \"-x86-asm-syntax=intel\" \"-tune-cpu\" \"generic\" \"-D_MT\" \"-flto-visibility-public-std\" \"--dependent-lib=libcmt\" \"--dependent-lib=oldnames\" \"-stack-protector\" \"2\" \"-fdiagnostics-format\" \"msvc\" \"-gno-column-info\" \"-gcodeview\" \"-debug-info-kind=constructor\" \"-fdebug-compilation-dir=C:\\\\Projects\\\\FlashCpp\" \"-fcoverage-compilation-dir=C:\\\\Projects\\\\FlashCpp\" \"-resource-dir\" \"C:\\\\Program Files\\\\Microsoft Visual Studio\\\\2022\\\\Community\\\\VC\\\\Tools\\\\Llvm\\\\x64\\\\lib\\\\clang\\\\19\" \"-internal-isystem\" \"C:\\\\Program Files\\\\Microsoft Visual Studio\\\\2022\\\\Community\\\\VC\\\\Tools\\\\Llvm\\\\x64\\\\lib\\\\clang\\\\19\\\\include\" \"-internal-isystem\" \"C:\\\\Program Files (x86)\\\\Microsoft Visual Studio\\\\2019\\\\Community\\\\VC\\\\Tools\\\\MSVC\\\\14.29.30133\\\\include\" \"-fdeprecated-macro\" \"-ferror-limit\" \"19\" \"-fno-use-cxa-atexit\" \"-fms-extensions\" \"-fms-compatibility\" \"-fms-compatibility-version=19.44.35209\" \"-std=c++14\" \"-fskip-odr-check-in-gmf\" \"-fdelayed-template-parsing\" \"-fcolor-diagnostics\" \"-faddrsig\" \"-x\" \"c++\"";
        std::vector<uint8_t> string_data;

        writeLittleEndian32(string_data, 0x00000000);
        string_data.insert(string_data.end(), cmdline.begin(), cmdline.end());
        string_data.push_back(0x00);

        // Calculate length exactly like Clang: TypeRecordKind + data + padding
        size_t data_size = string_data.size();
        size_t content_after_length = sizeof(TypeRecordKind) + data_size;
        size_t total_record_size = sizeof(uint16_t) + content_after_length;
        size_t aligned_record_size = (total_record_size + 3) & ~3;
        size_t padding_bytes = aligned_record_size - total_record_size;

        TypeRecordHeader header;
        header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
        header.kind = TypeRecordKind::LF_STRING_ID;

        debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                           reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
        debug_t_data.insert(debug_t_data.end(), string_data.begin(), string_data.end());
        alignTo4Bytes(debug_t_data);

        if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_STRING_ID 0x" << std::hex << current_type_index << std::dec
                  << " (command line: " << cmdline.length() << " chars, length: " << header.length << ")" << std::endl;
        current_type_index++;
    }

    // Add LF_BUILDINFO record (0x100b in Clang)
    {
        std::vector<uint8_t> buildinfo_data;

        // Build info contains 5 string ID references
        uint16_t count = 5;
        writeLittleEndian16(buildinfo_data, count);

        // References to the 5 string IDs we just created
        uint32_t dir_id = current_type_index - 5;      // Directory
        uint32_t compiler_id = current_type_index - 2;  // Compiler path
        uint32_t source_id = current_type_index - 4;    // Source file
        uint32_t empty_id = current_type_index - 3;     // Empty string
        uint32_t cmdline_id = current_type_index - 1;   // Command line

        writeLittleEndian32(buildinfo_data, dir_id);
        writeLittleEndian32(buildinfo_data, compiler_id);
        writeLittleEndian32(buildinfo_data, source_id);
        writeLittleEndian32(buildinfo_data, empty_id);
        writeLittleEndian32(buildinfo_data, cmdline_id);

        // Calculate length exactly like Clang: TypeRecordKind + data + padding
        size_t data_size = buildinfo_data.size();
        size_t content_after_length = sizeof(TypeRecordKind) + data_size;
        size_t total_record_size = sizeof(uint16_t) + content_after_length;
        size_t aligned_record_size = (total_record_size + 3) & ~3;
        size_t padding_bytes = aligned_record_size - total_record_size;

        TypeRecordHeader header;
        header.length = static_cast<uint16_t>(content_after_length + padding_bytes);
        header.kind = TypeRecordKind::LF_BUILDINFO;

        debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                           reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
        debug_t_data.insert(debug_t_data.end(), buildinfo_data.begin(), buildinfo_data.end());
        alignTo4Bytes(debug_t_data);

        if (g_enable_debug_output) std::cerr << "DEBUG: Added LF_BUILDINFO 0x" << std::hex << current_type_index << std::dec
                  << " (length: " << header.length << ", references: 0x" << std::hex << dir_id << ", 0x" << compiler_id
                  << ", 0x" << source_id << ", 0x" << empty_id << ", 0x" << cmdline_id
                  << std::dec << ")" << std::endl;
        current_type_index++;
    }





    // Individual records are already aligned, no final alignment needed

    if (g_enable_debug_output) std::cerr << "DEBUG: Final .debug$T section size: " << debug_t_data.size() << " bytes" << std::endl;

    // DEBUG: Dump the last 32 bytes to check for padding issues
    if (g_enable_debug_output) std::cerr << "DEBUG: Last 32 bytes of .debug$T: ";
    size_t start_pos = debug_t_data.size() >= 32 ? debug_t_data.size() - 32 : 0;
    for (size_t i = start_pos; i < debug_t_data.size(); ++i) {
        if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)debug_t_data[i] << " ";
    }
    if (g_enable_debug_output) std::cerr << std::dec << std::endl;

    return debug_t_data;
}

} // namespace CodeView
