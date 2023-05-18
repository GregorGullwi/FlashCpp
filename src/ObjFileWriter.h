#pragma once

#include "coffi/coffi.hpp"
#include <string>
#include <array>

enum class SectionType : unsigned char
{
	TEXT,
	DATA,
	BSS,

	Count
};

class ObjectFileWriter {
public:
	ObjectFileWriter() {
		coffi_.create(COFFI::COFFI_ARCHITECTURE_PE);
		coffi_.get_header()->set_flags(IMAGE_FILE_32BIT_MACHINE); // Should this even be here?
		coffi_.create_optional_header(OH_MAGIC_PE32PLUS);
		section_text_ = coffi_.add_section(".text");
		section_text_->set_flags(IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_4BYTES);
		sections_by_type[static_cast<unsigned char>(SectionType::TEXT)] = section_text_;

		section_data_ = coffi_.add_section(".data");
		section_data_->set_flags(IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES);
		sections_by_type[static_cast<unsigned char>(SectionType::DATA)] = section_data_;

		section_bss_ = coffi_.add_section(".bss");
		section_bss_->set_flags(IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES);
		sections_by_type[static_cast<unsigned char>(SectionType::BSS)] = section_bss_;
	}

	void write(const std::string& filename) {
		coffi_.save(filename);
	}

	void add_function_symbol(const std::string& name) {
		auto symbol_func = coffi_.add_symbol(name);
		symbol_func->set_value(0);
		symbol_func->set_section_number(1);
		symbol_func->set_type(IMAGE_SYM_TYPE_NULL);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
	}

	void add_data(const std::vector<char>& data, SectionType section_type) {
		sections_by_type[static_cast<unsigned char>(section_type)]->append_data(data.data(), data.size());
	}

protected:
	COFFI::coffi coffi_;

	std::array<COFFI::section*, static_cast<unsigned char>(SectionType::Count)> sections_by_type;
	COFFI::section* section_text_;	// Code goes in here
	COFFI::section* section_data_;	// Initialized data
	COFFI::section* section_bss_;	// Uninitialized data
};
