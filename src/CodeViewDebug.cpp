#include "CodeViewDebug.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

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
        // Find the function and update its line information
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                func.line_offsets = current_function_lines_;
                func.file_id = current_function_file_id_;
                break;
            }
        }
    }

    // Set up new current function
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

void DebugInfoBuilder::finalizeCurrentFunction() {
    if (!current_function_name_.empty()) {
        // Find the function and update its line information
        for (auto& func : functions_) {
            if (func.name == current_function_name_) {
                func.line_offsets = current_function_lines_;
                func.file_id = current_function_file_id_;
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
        return it->second;
    }
    
    uint32_t offset = static_cast<uint32_t>(string_table_.size());
    string_offsets_[str] = offset;
    
    // Add string with null terminator
    for (char c : str) {
        string_table_.push_back(static_cast<uint8_t>(c));
    }
    string_table_.push_back(0);
    
    return offset;
}

void DebugInfoBuilder::writeSymbolRecord(std::vector<uint8_t>& data, SymbolKind kind, const std::vector<uint8_t>& record_data) {
    SymbolRecordHeader header;
    // Length should be the size of the record excluding the length field itself
    // According to CodeView spec: "each record begins with a 16-bit record size and a 16-bit record kind"
    // So length = sizeof(kind) + record_data.size() (excluding the length field itself)
    header.length = static_cast<uint16_t>(sizeof(SymbolKind) + record_data.size());
    header.kind = kind;

    // Write header
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&header),
                reinterpret_cast<const uint8_t*>(&header) + sizeof(header));

    // Write record data
    data.insert(data.end(), record_data.begin(), record_data.end());

    // Align to 4-byte boundary
    alignTo4Bytes(data);
}

void DebugInfoBuilder::writeSubsection(std::vector<uint8_t>& data, DebugSubsectionKind kind, const std::vector<uint8_t>& subsection_data) {
    DebugSubsectionHeader header;
    header.kind = kind;
    header.length = static_cast<uint32_t>(subsection_data.size());
    
    // Write header
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&header), 
                reinterpret_cast<const uint8_t*>(&header) + sizeof(header));
    
    // Write subsection data
    data.insert(data.end(), subsection_data.begin(), subsection_data.end());
    
    // Align to 4-byte boundary
    alignTo4Bytes(data);
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

    for (const auto& func : functions_) {
        if (func.line_offsets.empty()) {
            continue; // Skip functions without line information
        }

        // Line info header
        LineInfoHeader line_header;
        line_header.code_offset = func.code_offset;
        line_header.segment = 1; // .text section
        line_header.flags = 0;   // No special flags
        line_header.code_length = func.code_length;

        // Write line info header
        line_data.insert(line_data.end(),
                        reinterpret_cast<const uint8_t*>(&line_header),
                        reinterpret_cast<const uint8_t*>(&line_header) + sizeof(line_header));

        // File block header
        FileBlockHeader file_header;
        file_header.file_id = func.file_id;
        file_header.num_lines = static_cast<uint32_t>(func.line_offsets.size());

        // Calculate block size: file header + line entries
        uint32_t block_size = sizeof(FileBlockHeader) +
                             (func.line_offsets.size() * sizeof(LineNumberEntry));
        file_header.block_size = block_size;

        // Write file block header
        line_data.insert(line_data.end(),
                        reinterpret_cast<const uint8_t*>(&file_header),
                        reinterpret_cast<const uint8_t*>(&file_header) + sizeof(file_header));

        // Write line number entries
        for (const auto& line_offset : func.line_offsets) {
            LineNumberEntry line_entry;
            line_entry.offset = line_offset.first;  // Code offset
            line_entry.line_start = line_offset.second; // Line number
            line_entry.delta_line_end = 0; // Single line statement
            line_entry.is_statement = 1;   // This is a statement

            line_data.insert(line_data.end(),
                           reinterpret_cast<const uint8_t*>(&line_entry),
                           reinterpret_cast<const uint8_t*>(&line_entry) + sizeof(line_entry));
        }

        // Align to 4-byte boundary
        alignTo4Bytes(line_data);
    }

    return line_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateDebugS() {
    std::vector<uint8_t> debug_s_data;

    // Write CodeView signature
    uint32_t signature = DEBUG_S_SIGNATURE;
    debug_s_data.insert(debug_s_data.end(), reinterpret_cast<const uint8_t*>(&signature),
                        reinterpret_cast<const uint8_t*>(&signature) + sizeof(signature));

    // Generate symbols subsection
    std::vector<uint8_t> symbols_data;
    
    // Add OBJNAME symbol
    {
        std::vector<uint8_t> objname_data;
        uint32_t signature_val = 0; // Signature
        objname_data.insert(objname_data.end(), reinterpret_cast<const uint8_t*>(&signature_val), 
                           reinterpret_cast<const uint8_t*>(&signature_val) + sizeof(signature_val));
        
        std::string obj_name = "FlashCpp.obj";
        for (char c : obj_name) {
            objname_data.push_back(static_cast<uint8_t>(c));
        }
        objname_data.push_back(0); // Null terminator
        
        writeSymbolRecord(symbols_data, SymbolKind::S_OBJNAME, objname_data);
    }
    
    // Add COMPILE3 symbol
    {
        std::vector<uint8_t> compile_data;
        
        // Language (C++)
        uint32_t language = 0x04; // CV_CFL_CXX
        compile_data.insert(compile_data.end(), reinterpret_cast<const uint8_t*>(&language), 
                           reinterpret_cast<const uint8_t*>(&language) + sizeof(language));
        
        // Target processor
        uint16_t target_processor = 0xD0; // CV_CFL_AMD64
        compile_data.insert(compile_data.end(), reinterpret_cast<const uint8_t*>(&target_processor), 
                           reinterpret_cast<const uint8_t*>(&target_processor) + sizeof(target_processor));
        
        // Flags
        uint32_t flags = 0; // No special flags
        compile_data.insert(compile_data.end(), reinterpret_cast<const uint8_t*>(&flags), 
                           reinterpret_cast<const uint8_t*>(&flags) + sizeof(flags));
        
        // Version strings
        std::string compiler_version = "FlashCpp 1.0";
        for (char c : compiler_version) {
            compile_data.push_back(static_cast<uint8_t>(c));
        }
        compile_data.push_back(0); // Null terminator
        
        writeSymbolRecord(symbols_data, SymbolKind::S_COMPILE3, compile_data);
    }
    
    // Add function symbols
    for (const auto& func : functions_) {
        std::vector<uint8_t> proc_data;
        
        // Parent, end, next pointers (set to 0 for now)
        uint32_t parent = 0, end = 0, next = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&parent), 
                        reinterpret_cast<const uint8_t*>(&parent) + sizeof(parent));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&end), 
                        reinterpret_cast<const uint8_t*>(&end) + sizeof(end));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&next), 
                        reinterpret_cast<const uint8_t*>(&next) + sizeof(next));
        
        // Code size and offset
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&func.code_length), 
                        reinterpret_cast<const uint8_t*>(&func.code_length) + sizeof(func.code_length));
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&func.code_offset), 
                        reinterpret_cast<const uint8_t*>(&func.code_offset) + sizeof(func.code_offset));
        
        // Debug start/end offsets (same as code for now)
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&func.code_offset), 
                        reinterpret_cast<const uint8_t*>(&func.code_offset) + sizeof(func.code_offset));
        uint32_t debug_end = func.code_offset + func.code_length;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&debug_end), 
                        reinterpret_cast<const uint8_t*>(&debug_end) + sizeof(debug_end));
        
        // Type index (0 for now)
        uint32_t type_index = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&type_index), 
                        reinterpret_cast<const uint8_t*>(&type_index) + sizeof(type_index));
        
        // Code segment and flags
        uint16_t segment = 1; // .text section
        uint8_t flags = 0;
        proc_data.insert(proc_data.end(), reinterpret_cast<const uint8_t*>(&segment), 
                        reinterpret_cast<const uint8_t*>(&segment) + sizeof(segment));
        proc_data.push_back(flags);
        
        // Function name (null-terminated for C13 format)
        for (char c : func.name) {
            proc_data.push_back(static_cast<uint8_t>(c));
        }
        proc_data.push_back(0); // Null terminator
        
        writeSymbolRecord(symbols_data, SymbolKind::S_GPROC32, proc_data);

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
            addr_range.section_start = 1; // .text section
            addr_range.length = static_cast<uint16_t>(var.end_offset - var.start_offset);

            defrange_data.insert(defrange_data.end(), reinterpret_cast<const uint8_t*>(&addr_range),
                                reinterpret_cast<const uint8_t*>(&addr_range) + sizeof(addr_range));

            writeSymbolRecord(symbols_data, SymbolKind::S_DEFRANGE_FRAMEPOINTER_REL, defrange_data);
        }

        // Add S_END symbol for function
        std::vector<uint8_t> end_data; // Empty for S_END
        writeSymbolRecord(symbols_data, SymbolKind::S_END, end_data);
    }
    
    // Write symbols subsection
    // Note: Symbols subsection disabled - was causing linker corruption errors
    // The FileChecksums, Lines, and StringTable subsections provide sufficient debug info
    // writeSubsection(debug_s_data, DebugSubsectionKind::Symbols, symbols_data);

    // Generate and write file checksums subsection
    auto checksum_data = generateFileChecksums();
    if (!checksum_data.empty()) {
        writeSubsection(debug_s_data, DebugSubsectionKind::FileChecksums, checksum_data);
    }

    // Generate and write line information subsection
    auto line_info_data = generateLineInfo();
    if (!line_info_data.empty()) {
        writeSubsection(debug_s_data, DebugSubsectionKind::Lines, line_info_data);
    }

    // Generate string table subsection
    writeSubsection(debug_s_data, DebugSubsectionKind::StringTable, string_table_);

    return debug_s_data;
}

std::vector<uint8_t> DebugInfoBuilder::generateDebugT() {
    std::vector<uint8_t> debug_t_data;
    
    // Write CodeView signature
    uint32_t signature = DEBUG_T_SIGNATURE;
    debug_t_data.insert(debug_t_data.end(), reinterpret_cast<const uint8_t*>(&signature), 
                        reinterpret_cast<const uint8_t*>(&signature) + sizeof(signature));
    
    // For now, just add basic type information
    // We'll expand this when we add proper type support
    
    return debug_t_data;
}

} // namespace CodeView
