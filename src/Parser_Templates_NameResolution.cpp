// Extract base template name from a mangled template instantiation name
// Supports underscore-based naming: "enable_if_void_int" -> "enable_if"
// Future: Will support hash-based naming: "enable_if$abc123" -> "enable_if"
// 
// Tries progressively longer prefixes by searching for '_' separators
// until a registered template or alias template is found.
//
// Returns: base template name if found, empty string_view otherwise
std::string_view Parser::extract_base_template_name(std::string_view mangled_name) {
	// Try progressively longer prefixes until we find a registered template
	size_t underscore_pos = 0;
	
	while ((underscore_pos = mangled_name.find('_', underscore_pos)) != std::string_view::npos) {
		std::string_view candidate = mangled_name.substr(0, underscore_pos);
		
		// Check if this is a registered class template
		auto candidate_opt = gTemplateRegistry.lookupTemplate(candidate);
		if (candidate_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name: found template '", 
			          candidate, "' in mangled name '", mangled_name, "'");
			return candidate;
		}
		
		// Also check alias templates
		auto alias_candidate = gTemplateRegistry.lookup_alias_template(candidate);
		if (alias_candidate.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name: found alias template '", 
			          candidate, "' in mangled name '", mangled_name, "'");
			return candidate;
		}
		
		underscore_pos++; // Move past this underscore
	}
	
	return {};  // Not found
}

// Extract base template name by stripping suffixes from right to left
// Used when we have an instantiated name like "Container_int_float"
// and need to find "Container"
//
// Returns: base template name if found, empty string_view otherwise
std::string_view Parser::extract_base_template_name_by_stripping(std::string_view instantiated_name) {
	std::string_view base_template_name = instantiated_name;
	
	// Try progressively stripping '_suffix' patterns until we find a registered template
	while (!base_template_name.empty()) {
		// Check if current name is a registered template
		auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
		if (template_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name_by_stripping: found template '", 
			          base_template_name, "' by stripping from '", instantiated_name, "'");
			return base_template_name;
		}
		
		// Also check alias templates
		auto alias_opt = gTemplateRegistry.lookup_alias_template(base_template_name);
		if (alias_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name_by_stripping: found alias template '", 
			          base_template_name, "' by stripping from '", instantiated_name, "'");
			return base_template_name;
		}
		
		// Strip last suffix
		size_t underscore_pos = base_template_name.find_last_of('_');
		if (underscore_pos == std::string_view::npos) {
			break;  // No more underscores to strip
		}
		
		base_template_name = base_template_name.substr(0, underscore_pos);
	}
	
	return {};  // Not found
}
