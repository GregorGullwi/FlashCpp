#pragma once

#include "coffi/coffi.hpp"
#include <string>
#include <array>
#include <chrono>

enum class SectionType : unsigned char
{
	TEXT,
	DATA,
	BSS,
	DRECTVE,

	Count
};

class ObjectFileWriter {
public:
	ObjectFileWriter() {
		coffi_.create(COFFI::COFFI_ARCHITECTURE_PE);
		coffi_.get_header()->set_machine(IMAGE_FILE_MACHINE_AMD64);
		auto now = std::chrono::system_clock::now();
		std::time_t current_time_t = std::chrono::system_clock::to_time_t(now);
		coffi_.get_header()->set_time_data_stamp(static_cast<uint32_t>(current_time_t));

		auto section_drectve = add_section(".drectve", IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE, SectionType::DRECTVE);
		section_drectve->append_data(" /DEFAULTLIB:\"LIBCMT\" "); // MSVC also contains '/DEFAULTLIB:\"OLDNAMES\" ', but it doesn't seem to be needed?
		auto symbol_drectve = coffi_.add_symbol(".drectve");
		symbol_drectve->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_drectve->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_drectve->set_section_number(section_drectve->get_index() + 1);

		auto section_text = add_section(".text$mn", IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_16BYTES, SectionType::TEXT);
		auto symbol_text = coffi_.add_symbol(".text$mn");
		symbol_text->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_text->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_text->set_section_number(section_text->get_index() + 1);
	}

	COFFI::section* add_section(const std::string& section_name, int32_t flags, std::optional<SectionType> section_type) {
		COFFI::section* section = coffi_.add_section(section_name);
		section->set_flags(flags);
		if (section_type.has_value()) {
			sectiontype_to_index[*section_type] = section->get_index();
			sectiontype_to_name[*section_type] = section_name;
		}
		return section;
	}

	void write(const std::string& filename) {
		coffi_.save(filename);
	}

	void add_function_symbol(const std::string& name, uint32_t section_offset) {
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol_func = coffi_.add_symbol(name);
		symbol_func->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol_func->set_section_number(section_text->get_index() + 1);
		symbol_func->set_value(section_offset);
	}

	void add_data(const std::vector<char>& data, SectionType section_type) {
		coffi_.get_sections()[sectiontype_to_index[section_type]]->append_data(data.data(), data.size());
	}

	void add_relocation(uint64_t offset, std::string_view symbol_name) {
		auto* symbol = coffi_.get_symbol(std::string(symbol_name));
		if (!symbol) {
			throw std::runtime_error("Symbol not found: " + std::string(symbol_name));
		}

		auto symbol_index = symbol->get_index();
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		COFFI::rel_entry_generic relocation;
		relocation.virtual_address = offset;
		relocation.symbol_table_index = symbol_index;
		relocation.type = IMAGE_REL_AMD64_REL32;
		section_text->add_relocation_entry(&relocation);
	}

protected:
	COFFI::coffi coffi_;
	std::unordered_map<SectionType, std::string> sectiontype_to_name;
	std::unordered_map<SectionType, int32_t> sectiontype_to_index;
};
