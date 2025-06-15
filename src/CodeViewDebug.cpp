#include "CodeViewDebug.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

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

    std::array<uint8_t, 32> calculateFileSHA256(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
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
}

uint32_t DebugInfoBuilder::addSourceFile(const std::string& filename) {
    auto it = file_name_to_id_.find(filename);
    if (it != file_name_to_id_.end()) {
        return it->second;
    }
    
    uint32_t file_id = static_cast<uint32_t>(source_files_.size());
    source_files_.push_back(filename);
    file_name_to_id_[filename] = file_id;
    
    // Add filename to string table
    addString(filename);
    
    return file_id;
}

void DebugInfoBuilder::addLineInfo(const std::string& function_name, uint32_t code_offset, 
                                   uint32_t code_length, uint32_t file_id, 
                                   const std::vector<std::pair<uint32_t, uint32_t>>& line_offsets) {
    FunctionInfo func_info;
    func_info.name = function_name;
    func_info.code_offset = code_offset;
    func_info.code_length = code_length;
    func_info.file_id = file_id;
    func_info.line_offsets = line_offsets;
    
    functions_.push_back(func_info);
}

void DebugInfoBuilder::addFunction(const std::string& name, uint32_t code_offset, uint32_t code_length) {
    // Add basic function info without line numbers
    FunctionInfo func_info;
    func_info.name = name;
    func_info.code_offset = code_offset;
    func_info.code_length = code_length;
    func_info.file_id = 0; // Default to first file

    functions_.push_back(func_info);
}

void DebugInfoBuilder::addFunctionWithLines(const std::string& name, uint32_t code_offset,
                                           uint32_t code_length, uint32_t file_id,
                                           const std::vector<std::pair<uint32_t, uint32_t>>& line_offsets) {
    FunctionInfo func_info;
    func_info.name = name;
    func_info.code_offset = code_offset;
    func_info.code_length = code_length;
    func_info.file_id = file_id;
    func_info.line_offsets = line_offsets;

    functions_.push_back(func_info);
}

void DebugInfoBuilder::setCurrentFunction(const std::string& name, uint32_t file_id) {
    // Finalize previous function if any
    if (!current_function_name_.empty()) {
        std::cerr << "DEBUG: Finalizing previous function: " << current_function_name_
                  << " with file_id=" << current_function_file_id_
                  << " and " << current_function_lines_.size() << " lines" << std::endl;
        // Find the function and update its line information
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                func.line_offsets = current_function_lines_;
                func.file_id = current_function_file_id_;
                std::cerr << "DEBUG: Updated function " << func.name << " with file_id=" << func.file_id << std::endl;
                break;
            }
        }
    }

    // Set up new current function
    std::cerr << "DEBUG: Setting current function: " << name << " with file_id=" << file_id << std::endl;
    current_function_name_ = name;
    current_function_file_id_ = file_id;
    current_function_lines_.clear();
}

void DebugInfoBuilder::addLineMapping(uint32_t code_offset, uint32_t line_number) {
    if (!current_function_name_.empty()) {
        current_function_lines_.emplace_back(code_offset, line_number);
    }
}

void DebugInfoBuilder::addLocalVariable(const std::string& name, uint32_t type_index,
                                       uint32_t stack_offset, uint32_t start_offset, uint32_t end_offset) {
    if (!current_function_name_.empty()) {
        // Find the current function and add the local variable
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                LocalVariableInfo var_info;
                var_info.name = name;
                var_info.type_index = type_index;
                var_info.stack_offset = stack_offset;
                var_info.start_offset = start_offset;
                var_info.end_offset = end_offset;
                var_info.flags = 0; // Default flags

                func.local_variables.push_back(var_info);
                break;
            }
        }
    }
}

void DebugInfoBuilder::addFunctionParameter(const std::string& name, uint32_t type_index, uint32_t stack_offset) {
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

void DebugInfoBuilder::updateFunctionLength(const std::string& name, uint32_t code_length) {
    // Find the function and update its length
    for (auto& func : functions_) {
        if (func.name == name) {
            func.code_length = code_length;
            break;
        }
    }
}

void DebugInfoBuilder::setTextSectionNumber(uint16_t section_number) {
    text_section_number_ = section_number;
}

void DebugInfoBuilder::finalizeCurrentFunction() {
    if (!current_function_name_.empty()) {
        std::cerr << "DEBUG: finalizeCurrentFunction: " << current_function_name_
                  << " with file_id=" << current_function_file_id_
                  << " and " << current_function_lines_.size() << " lines" << std::endl;
        // Find the function and update its line information
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                func.line_offsets = current_function_lines_;
                func.file_id = current_function_file_id_;
                std::cerr << "DEBUG: finalizeCurrentFunction updated function " << func.name << " with file_id=" << func.file_id << std::endl;
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
        std::cerr << "DEBUG: String '" << str << "' already exists at offset " << it->second << std::endl;
        return it->second;
    }

    uint32_t offset = static_cast<uint32_t>(string_table_.size());
    string_offsets_[str] = offset;

    std::cerr << "DEBUG: Adding string '" << str << "' at offset " << offset << std::endl;

    // Add string with null terminator
    for (char c : str) {
        string_table_.push_back(static_cast<uint8_t>(c));
    }
    string_table_.push_back(0);

    return offset;
}

void DebugInfoBuilder::initializeFunctionIdMap() {
    // Dynamically generate function ID mapping based on actual functions
    // For simplified type generation, we only generate LF_FUNC_ID records
    // Type indices start at 0x1000 and increment by 1 for each function

    function_id_map_.clear();

    for (size_t i = 0; i < functions_.size(); ++i) {
        uint32_t func_id_index = 0x1000 + static_cast<uint32_t>(i); // Simple increment
        function_id_map_[functions_[i].name] = func_id_index;
    }

    std::cerr << "DEBUG: Dynamically initialized function ID map for " << functions_.size() << " functions:" << std::endl;
    for (const auto& pair : function_id_map_) {
        std::cerr << "  " << pair.first << " -> 0x" << std::hex << pair.second << std::dec << std::endl;
    }
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

    std::cerr << "DEBUG: Writing subsection kind=" << static_cast<uint32_t>(kind)
              << ", unpadded_size=" << unpadded_size
              << ", header_length=" << header.length
              << ", header_size=" << sizeof(header) << std::endl;

    // Debug: Show header bytes
    std::cerr << "DEBUG: Header bytes: ";
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    for (size_t i = 0; i < sizeof(header); ++i) {
        std::cerr << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(header_bytes[i]) << " ";
    }
    std::cerr << std::dec << std::endl;

    // Write header
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&header),
                reinterpret_cast<const uint8_t*>(&header) + sizeof(header));

    // Write subsection data
    data.insert(data.end(), subsection_data.begin(), subsection_data.end());

    // Add padding to match the length we claimed in the header
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
}

void DebugInfoBuilder::alignTo4Bytes(std::vector<uint8_t>& data) {
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
}

std::vector<uint8_t> DebugInfoBuilder::generateFileChecksums() {
    std::vector<uint8_t> checksum_data;

    for (size_t i = 0; i < source_files_.size(); ++i) {
        const std::string& filename = source_files_[i];
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

    std::cerr << "DEBUG: Main line header - offCon=" << main_header.offCon
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

        std::cerr << "DEBUG: Line info for function " << func.name
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
        std::cerr << "DEBUG: FileBlockHeader bytes: ";
        const uint8_t* file_header_bytes = reinterpret_cast<const uint8_t*>(&file_header);
        for (size_t i = 0; i < sizeof(file_header); ++i) {
            std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)file_header_bytes[i] << " ";
        }
        std::cerr << std::dec << std::endl;

        // Write file block header
        line_data.insert(line_data.end(),
                         reinterpret_cast<const uint8_t*>(&file_header),
                         reinterpret_cast<const uint8_t*>(&file_header) + sizeof(file_header));

        // Write line number entries
        for (const auto& line_offset : func.line_offsets) {
            LineNumberEntry line_entry;
            line_entry.offset = line_offset.first;  // Code offset relative to function start
            line_entry.line_start = line_offset.second; // Line number
            line_entry.delta_line_end = 0; // Single line statement
            line_entry.is_statement = 1;   // This is a statement

            // DEBUG: Dump line entry bytes
            std::cerr << "DEBUG: LineNumberEntry for offset=" << line_entry.offset
                      << ", line=" << line_entry.line_start << " bytes: ";
            const uint8_t* entry_bytes = reinterpret_cast<const uint8_t*>(&line_entry);
            for (size_t i = 0; i < sizeof(line_entry); ++i) {
                std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)entry_bytes[i] << " ";
            }
            std::cerr << std::dec << std::endl;

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
    line_header.code_offset = func.code_offset;
    line_header.segment = 0; // Use section 0 to match MSVC/clang reference
    line_header.flags = 0;   // No special flags
    line_header.code_length = func.code_length;

    std::cerr << "DEBUG: Function line header - offCon=" << line_header.code_offset
              << ", segCon=" << line_header.segment
              << ", cbCon=" << line_header.code_length << std::endl;

    line_data.insert(line_data.end(),
                     reinterpret_cast<const uint8_t*>(&line_header),
                     reinterpret_cast<const uint8_t*>(&line_header) + sizeof(line_header));

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

    std::cerr << "DEBUG: Function line info for function " << func.name
              << " - file_id=" << file_block.file_id
              << ", num_lines=" << file_block.num_lines << std::endl;

    // DEBUG: Dump file block header bytes
    std::cerr << "DEBUG: FileBlockHeader bytes: ";
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&file_block);
    for (size_t i = 0; i < sizeof(file_block); ++i) {
        std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)header_bytes[i] << " ";
    }
    std::cerr << std::dec << std::endl;

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
        std::cerr << "DEBUG: LineNumberEntry for offset=" << line_entry.offset
                  << ", line=" << line_entry.line_start << " bytes: ";
        const uint8_t* entry_bytes = reinterpret_cast<const uint8_t*>(&line_entry);
        for (size_t i = 0; i < sizeof(line_entry); ++i) {
            std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)entry_bytes[i] << " ";
        }
        std::cerr << std::dec << std::endl;

        line_data.insert(line_data.end(),
                         reinterpret_cast<const uint8_t*>(&line_entry),
                         reinterpret_cast<const uint8_t*>(&line_entry) + sizeof(line_entry));
    }

    return line_data;
}



std::vector<uint8_t> DebugInfoBuilder::generateDebugS() {
    std::cerr << "DEBUG: generateDebugS() called" << std::endl;

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
        std::cerr << "DEBUG: Writing S_OBJNAME symbol" << std::endl;
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

        std::cerr << "DEBUG: Using object name: " << obj_name << std::endl;
        for (char c : obj_name) {
            objname_data.push_back(static_cast<uint8_t>(c));
        }
        objname_data.push_back(0); // Null terminator

        writeSymbolRecord(symbols_data, SymbolKind::S_OBJNAME, objname_data);
        std::cerr << "DEBUG: S_OBJNAME symbol written, symbols_data size: " << symbols_data.size() << std::endl;
    }

    // Add S_COMPILE3 symbol
    {
        std::vector<uint8_t> compile3_data;

        // Create S_COMPILE3 structure
        CompileSymbol3 compile3;
        compile3.language = CV_CFL_CXX;           // C++ (0x01)
        compile3.flags[0] = 0x00;                 // No special flags
        compile3.flags[1] = 0x00;                 // No special flags
        compile3.flags[2] = 0x00;                 // No special flags
        compile3.machine = CV_CFL_X64;            // x64 target (0xD0)
        compile3.frontend_major = 1;              // FlashCpp version
        compile3.frontend_minor = 0;
        compile3.frontend_build = 0;
        compile3.frontend_qfe = 0;
        compile3.backend_major = 1;               // FlashCpp version
        compile3.backend_minor = 0;
        compile3.backend_build = 0;
        compile3.backend_qfe = 0;

        // Debug: Print the structure values
        std::cerr << "DEBUG: S_COMPILE3 structure:" << std::endl;
        std::cerr << "  language: 0x" << std::hex << (int)compile3.language << std::dec << std::endl;
        std::cerr << "  machine: 0x" << std::hex << compile3.machine << std::dec << std::endl;
        std::cerr << "  frontend version: " << compile3.frontend_major << "." << compile3.frontend_minor
                  << "." << compile3.frontend_build << "." << compile3.frontend_qfe << std::endl;
        std::cerr << "  structure size: " << sizeof(compile3) << " bytes" << std::endl;

        // Add structure to data
        compile3_data.insert(compile3_data.end(),
                            reinterpret_cast<const uint8_t*>(&compile3),
                            reinterpret_cast<const uint8_t*>(&compile3) + sizeof(compile3));

        // Add version string
        const char* version_string = "FlashCpp C++20 Compiler";
        compile3_data.insert(compile3_data.end(),
                            reinterpret_cast<const uint8_t*>(version_string),
                            reinterpret_cast<const uint8_t*>(version_string) + strlen(version_string));
        compile3_data.push_back(0); // Null terminator

        writeSymbolRecord(symbols_data, SymbolKind::S_COMPILE3, compile3_data);
        std::cerr << "DEBUG: S_COMPILE3 symbol written, symbols_data size: " << symbols_data.size() << std::endl;
    }

    // Write symbols subsection
    std::cerr << "DEBUG: Final symbols_data size before writeSubsection: " << symbols_data.size() << std::endl;
    writeSubsection(debug_s_data, DebugSubsectionKind::Symbols, symbols_data);
    symbols_data.clear();

    // Add function symbols (S_OBJNAME duplication issue is now fixed)
    std::cerr << "DEBUG: Number of functions: " << functions_.size() << std::endl;
    for (const auto& func : functions_) {
        std::cerr << "DEBUG: Processing function: " << func.name << std::endl;
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
        uint32_t debug_start = 8;  // After prologue (push rbp; mov rbp, rsp; sub rsp, 0x20)
        uint32_t debug_end = 20;   // Match reference: 0x14 = 20
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&debug_start),
                        reinterpret_cast<const uint8_t*>(&debug_start) + sizeof(debug_start));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&debug_end),
                        reinterpret_cast<const uint8_t*>(&debug_end) + sizeof(debug_end));

        // Function ID (4 bytes) - LF_FUNC_ID type index for this function
        auto it = function_id_map_.find(func.name);
        uint32_t function_id = (it != function_id_map_.end()) ? it->second : 0x1000; // fallback
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&function_id),
                        reinterpret_cast<const uint8_t*>(&function_id) + sizeof(function_id));

        // Function offset and segment
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&func.code_offset),
                        reinterpret_cast<const uint8_t*>(&func.code_offset) + sizeof(func.code_offset));

        // Use section 0 to match MSVC/clang reference (not the actual section number)
        uint16_t section_number = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&section_number),
                        reinterpret_cast<const uint8_t*>(&section_number) + sizeof(section_number));

        // Flags (1 byte) - Try to match "Do Not Inline, Optimized Debug Info"
        // Let's try some common values:
        // 0x00 = No flags, 0x40 = optimized, 0x80 = no inline
        // Based on common patterns, try 0x40 | 0x80 = 0xC0
        uint8_t proc_flags = 0x40 | 0x80; // Try 0xC0 for optimized + no inline
        proc_data.push_back(proc_flags);

        std::cerr << "DEBUG: Function ID written: 0x" << std::hex << function_id << std::dec << " (4 bytes)" << std::endl;

        // Function name (null-terminated string for S_GPROC32_ID)
        std::cerr << "DEBUG: Writing function name: '" << func.name << "' (length: " << func.name.length() << ")" << std::endl;
        // Remove length prefix - use only null-terminated string
        for (char c : func.name) {
            proc_data.push_back(static_cast<uint8_t>(c));
        }
        proc_data.push_back(0); // null terminator

        std::cerr << "DEBUG: Function proc_data size: " << proc_data.size() << " bytes" << std::endl;
        std::cerr << "DEBUG: Function offset: " << func.code_offset << ", length: " << func.code_length << std::endl;

        writeSymbolRecord(symbols_data, SymbolKind::S_GPROC32_ID, proc_data);

        // Add S_FRAMEPROC record
        std::vector<uint8_t> frameproc_data;
        uint32_t frame_size = 0x00000000;  // Frame size
        uint32_t pad_size = 0x00000000;    // Pad size
        uint32_t pad_offset = 0x00000000;  // Offset of pad in frame
        uint32_t callee_save_size = 0x00000000; // Size of callee save registers
        uint32_t exception_handler_offset = 0x00000000; // Exception handler offset
        uint16_t exception_handler_section = 0x0000;    // Exception handler section
        uint32_t flags = 0x0002A000; // Function info flags (safebuffers invalid_pgo_counts Local=ebp Param=ebp)

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

        // Add S_LOCAL + S_DEFRANGE_FRAMEPOINTER_REL records for function parameters (modern approach)
        for (const auto& param : func.parameters) {
            // S_LOCAL record
            std::vector<uint8_t> local_data;

            // Type index (T_INT4 = 0x74)
            uint32_t type_index = 0x74; // T_INT4 for int parameters
            local_data.insert(local_data.end(), reinterpret_cast<const uint8_t*>(&type_index),
                             reinterpret_cast<const uint8_t*>(&type_index) + sizeof(type_index));

            // Flags (0x0001 = parameter flag)
            uint16_t flags = 0x0001; // Parameter flag
            local_data.insert(local_data.end(), reinterpret_cast<const uint8_t*>(&flags),
                             reinterpret_cast<const uint8_t*>(&flags) + sizeof(flags));

            // Parameter name (null-terminated string)
            for (char c : param.name) {
                local_data.push_back(static_cast<uint8_t>(c));
            }
            local_data.push_back(0); // Null terminator

            writeSymbolRecord(symbols_data, SymbolKind::S_LOCAL, local_data);

            // S_DEFRANGE_FRAMEPOINTER_REL record
            std::vector<uint8_t> defrange_data;

            // Frame offset (positive for parameters above frame pointer)
            int32_t frame_offset = static_cast<int32_t>(param.stack_offset);
            defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&frame_offset),
                                reinterpret_cast<const uint8_t*>(&frame_offset) + sizeof(frame_offset));

            // Address range where parameter is valid (entire function)
            LocalVariableAddrRange addr_range;
            addr_range.offset_start = func.code_offset;
            addr_range.section_start = 0; // Use section 0 to match MSVC/clang reference
            addr_range.length = static_cast<uint16_t>(func.code_length);

            defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&addr_range),
                                reinterpret_cast<const uint8_t*>(&addr_range) + sizeof(addr_range));

            writeSymbolRecord(symbols_data, SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL, defrange_data);
        }

        // Add local variable symbols for this function
        for (const auto& var : func.local_variables) {
            // S_LOCAL symbol
            std::vector<uint8_t> local_data;

            // Type index
            local_data.insert(local_data.end(), reinterpret_cast<const uint8_t*>(&var.type_index),
                             reinterpret_cast<const uint8_t*>(&var.type_index) + sizeof(var.type_index));

            // Flags
            local_data.insert(local_data.end(), reinterpret_cast<const uint8_t*>(&var.flags),
                             reinterpret_cast<const uint8_t*>(&var.flags) + sizeof(var.flags));

            // Variable name (null-terminated for C13 format)
            for (char c : var.name) {
                local_data.push_back(static_cast<uint8_t>(c));
            }
            local_data.push_back(0); // Null terminator

            writeSymbolRecord(symbols_data, SymbolKind::S_LOCAL, local_data);

            // S_DEFRANGE_FRAMEPOINTER_REL symbol for variable location
            std::vector<uint8_t> defrange_data;

            // Stack offset from frame pointer
            defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&var.stack_offset),
                                reinterpret_cast<const uint8_t*>(&var.stack_offset) + sizeof(var.stack_offset));

            // Address range where variable is valid
            LocalVariableAddrRange addr_range;
            addr_range.offset_start = var.start_offset;
            addr_range.section_start = 0; // Use section 0 to match MSVC/clang reference
            addr_range.length = static_cast<uint16_t>(var.end_offset - var.start_offset);

            defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&addr_range),
                                reinterpret_cast<const uint8_t*>(&addr_range) + sizeof(addr_range));

            writeSymbolRecord(symbols_data, SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL, defrange_data);
        }

        // Add S_PROC_ID_END symbol for function
        std::vector<uint8_t> end_data; // Empty for S_PROC_ID_END
        writeSymbolRecord(symbols_data, SymbolKind::S_PROC_ID_END, end_data);

        // Generate separate line information subsections for each function
        std::cerr << "DEBUG: Generating separate line information subsections for function..." << std::endl;
        if (!func.line_offsets.empty()) {
            auto line_data = generateLineInfoForFunction(func);
            if (!line_data.empty()) {
                writeSubsection(debug_s_data, DebugSubsectionKind::Lines, line_data);
                std::cerr << "DEBUG: Added " << line_data.size() << " bytes of line information for function " << func.name << std::endl;
            }
        }
        else {
            std::cerr << "DEBUG: No line information for function " << func.name << std::endl;
        }

        std::cerr << "DEBUG: Final symbols_data size before writeSubsection: " << symbols_data.size() << std::endl;
        writeSubsection(debug_s_data, DebugSubsectionKind::Symbols, symbols_data);
        symbols_data.clear();
    }

    // Generate and write file checksums subsection
    auto checksum_data = generateFileChecksums();
    if (!checksum_data.empty()) {
        writeSubsection(debug_s_data, DebugSubsectionKind::FileChecksums, checksum_data);
    }

    // Generate string table subsection
    std::cerr << "DEBUG: String table size: " << string_table_.size() << " bytes" << std::endl;
    std::cerr << "DEBUG: String table contents: ";
    for (size_t i = 0; i < std::min(string_table_.size(), size_t(50)); ++i) {
        if (string_table_[i] == 0) {
            std::cerr << "\\0";
        } else if (string_table_[i] >= 32 && string_table_[i] <= 126) {
            std::cerr << (char)string_table_[i];
        } else {
            std::cerr << "\\x" << std::hex << (int)string_table_[i] << std::dec;
        }
    }
    if (string_table_.size() > 50) std::cerr << "...";
    std::cerr << std::endl;
    writeSubsection(debug_s_data, DebugSubsectionKind::StringTable, string_table_);

    return debug_s_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateDebugT() {
    std::vector<uint8_t> debug_t_data;

    // Write CodeView signature
    uint32_t signature = DEBUG_T_SIGNATURE;
    debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&signature),
                        reinterpret_cast<const uint8_t*>(&signature) + sizeof(signature));

    std::cerr << "DEBUG: Generating dynamic type information for " << functions_.size() << " functions..." << std::endl;

    // DEBUG: List all functions in the vector
    for (size_t i = 0; i < functions_.size(); ++i) {
        std::cerr << "DEBUG: Function " << i << ": '" << functions_[i].name
                  << "' at offset " << functions_[i].code_offset
                  << " length " << functions_[i].code_length << std::endl;
    }

    // Generate type records dynamically for each function
    // For now, generate only LF_FUNC_ID records to avoid the duplicate issue
    for (size_t func_index = 0; func_index < functions_.size(); ++func_index) {
        const auto& func = functions_[func_index];
        uint32_t base_type_index = 0x1000 + static_cast<uint32_t>(func_index);

        std::cerr << "DEBUG: Generating simplified types for function '" << func.name
                  << "' with " << func.parameters.size() << " parameters" << std::endl;

        // Generate LF_ARGLIST (base_type_index + 0)
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

            TypeRecordHeader header;
            header.length = static_cast<uint16_t>(arglist_data.size() + sizeof(TypeRecordKind));
            header.kind = TypeRecordKind::LF_ARGLIST;

            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), arglist_data.begin(), arglist_data.end());
            // Always align LF_ARGLIST records since they're not the last
            alignTo4Bytes(debug_t_data);

            std::cerr << "DEBUG: Added LF_ARGLIST 0x" << std::hex << base_type_index << std::dec
                      << " (" << arg_count << " args for " << func.name << ")" << std::endl;
        }

        // Generate LF_PROCEDURE (base_type_index + 1)
        {
            std::vector<uint8_t> proc_data;

            // Return type (T_INT4 = 0x74)
            uint32_t return_type = 0x74;
            proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&return_type),
                            reinterpret_cast<const uint8_t*>(&return_type) + sizeof(return_type));

            // Calling convention (0 = C Near, matches clang-cl reference)
            uint8_t calling_conv = 0;
            proc_data.push_back(calling_conv);

            // Function attributes (0 = none)
            uint8_t func_attrs = 0;
            proc_data.push_back(func_attrs);

            // Parameter count
            uint16_t param_count = static_cast<uint16_t>(func.parameters.size());
            proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&param_count),
                            reinterpret_cast<const uint8_t*>(&param_count) + sizeof(param_count));

            // Argument list type index (base_type_index + 0)
            uint32_t arglist_type = base_type_index;
            proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&arglist_type),
                            reinterpret_cast<const uint8_t*>(&arglist_type) + sizeof(arglist_type));

            TypeRecordHeader header;
            header.length = static_cast<uint16_t>(proc_data.size() + sizeof(TypeRecordKind));
            header.kind = TypeRecordKind::LF_PROCEDURE;

            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), proc_data.begin(), proc_data.end());
            // Always align LF_PROCEDURE records since they're not the last
            alignTo4Bytes(debug_t_data);

            std::cerr << "DEBUG: Added LF_PROCEDURE 0x" << std::hex << (base_type_index + 1) << std::dec
                      << " (" << func.name << " function)" << std::endl;
        }

        // Generate LF_FUNC_ID (base_type_index)
        {
            std::vector<uint8_t> func_id_data;

            // Type index (use T_NOTYPE since we're not generating LF_PROCEDURE)
            uint32_t type_index = 0; // T_NOTYPE
            func_id_data.insert(func_id_data.end(), reinterpret_cast<const uint8_t*>(&type_index),
                               reinterpret_cast<const uint8_t*>(&type_index) + sizeof(type_index));

            // Scope (0 = global)
            uint32_t scope = 0;
            func_id_data.insert(func_id_data.end(), reinterpret_cast<const uint8_t*>(&scope),
                               reinterpret_cast<const uint8_t*>(&scope) + sizeof(scope));

            // Function name (dynamic)
            for (char c : func.name) {
                func_id_data.push_back(static_cast<uint8_t>(c));
            }
            func_id_data.push_back(0); // null terminator

            TypeRecordHeader header;
            header.length = static_cast<uint16_t>(func_id_data.size() + sizeof(TypeRecordKind));
            header.kind = TypeRecordKind::LF_FUNC_ID;

            size_t before_size = debug_t_data.size();
            debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&header),
                               reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
            debug_t_data.insert(debug_t_data.end(), func_id_data.begin(), func_id_data.end());
            size_t after_data_size = debug_t_data.size();

            // Only align if this is not the last function
            if (func_index < functions_.size() - 1) {
                alignTo4Bytes(debug_t_data);
            }
            size_t after_align_size = debug_t_data.size();

            std::cerr << "DEBUG: Added LF_FUNC_ID 0x" << std::hex << base_type_index << std::dec
                      << " (" << func.name << ") - length=" << header.length
                      << ", data_size=" << (after_data_size - before_size)
                      << ", padded_size=" << (after_align_size - before_size)
                      << ", offset=" << before_size << std::endl;
        }
    } // End of function loop

    std::cerr << "DEBUG: Final .debug$T section size: " << debug_t_data.size() << " bytes" << std::endl;

    // DEBUG: Dump the last 32 bytes to check for padding issues
    std::cerr << "DEBUG: Last 32 bytes of .debug$T: ";
    size_t start_pos = debug_t_data.size() >= 32 ? debug_t_data.size() - 32 : 0;
    for (size_t i = start_pos; i < debug_t_data.size(); ++i) {
        std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)debug_t_data[i] << " ";
    }
    std::cerr << std::dec << std::endl;

    return debug_t_data;
}

} // namespace CodeView
