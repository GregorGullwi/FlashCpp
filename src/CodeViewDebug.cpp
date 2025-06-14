#include "CodeViewDebug.h"
#include <cstring>
#include <algorithm>

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

        FileChecksumEntry entry;
        entry.file_name_offset = filename_offset;
        entry.checksum_size = 0;  // No checksum for now
        entry.checksum_kind = 0;  // No checksum

        // Write the entry
        checksum_data.insert(checksum_data.end(),
                           reinterpret_cast<const uint8_t*>(&entry),
                           reinterpret_cast<const uint8_t*>(&entry) + sizeof(entry));

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
        
        // Function name
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

            // Variable name
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
    writeSubsection(debug_s_data, DebugSubsectionKind::Symbols, symbols_data);

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
