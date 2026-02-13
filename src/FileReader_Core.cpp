class FileReader {
public:
	static constexpr size_t default_result_size = 1024 * 1024;
	FileReader(CompileContext& settings, FileTree& tree) : settings_(settings), tree_(tree) {
		addBuiltinDefines();
		result_.reserve(default_result_size);
	}
	
	// Get the line mapping for source location tracking
	const std::vector<SourceLineMapping>& get_line_map() const {
		return line_map_;
	}
	
	// Get the file paths vector for looking up source files from line map
	const std::vector<std::string>& get_file_paths() const {
		return file_paths_;
	}

	size_t find_first_non_whitespace_after_hash(const std::string& str) {
		size_t pos = str.find('#');
		if (pos == std::string::npos) {
			return pos;
		}
		return str.find_first_not_of(" \t", pos + 1);
	}

	bool readFile(std::string_view file, long included_at_line = 0) {
		if (proccessedHeaders_.find(std::string(file)) != proccessedHeaders_.end())
			return true;

		// Check for excessive include nesting depth (prevents infinite recursion)
		constexpr size_t MAX_INCLUDE_DEPTH = 200;
		if (filestack_.size() >= MAX_INCLUDE_DEPTH) {
			FLASH_LOG(Lexer, Error, "Include nesting depth exceeded ", MAX_INCLUDE_DEPTH, " (possible circular include)");
			return false;
		}

		if (settings_.isVerboseMode()) {
			std::cout << "readFile " << file << " (depth: " << filestack_.size() << ")" << std::endl;
		}

		// Save the current file index and parent line to restore when we return from this file
		size_t saved_file_index = current_file_index_;
		size_t saved_parent_line = current_parent_line_;
		
		// Register this file in the file_paths_ vector and update current index
		current_file_index_ = get_or_add_file_path(file);
		
		// The parent line is the preprocessed line number corresponding to the #include directive
		// in the parent file. We need to find that in the line_map.
		// For the main file, included_at_line is 0, so current_parent_line_ stays 0
		if (included_at_line > 0 && !line_map_.empty()) {
			// Find the most recent line_map entry for the parent file at the include line
			// We search backwards because we just processed that line
			for (size_t i = line_map_.size(); i > 0; --i) {
				if (line_map_[i - 1].source_file_index == saved_file_index &&
				    line_map_[i - 1].source_line == static_cast<size_t>(included_at_line)) {
					current_parent_line_ = i; // 1-based preprocessed line number
					break;
				}
			}
		} else {
			current_parent_line_ = 0;  // Main file has no parent
		}

		ScopedFileStack filestack(filestack_, file, included_at_line);

		// std::ifstream(const char*) expects a null-terminated path; string_view::data() is not guaranteed
		// to be null-terminated.
		std::string file_str(file);
		std::ifstream stream(file_str);
		if (!stream.is_open()) {
			current_file_index_ = saved_file_index;  // Restore on error
			current_parent_line_ = saved_parent_line;
			return false;
		}

		tree_.addFile(file);

		stream.seekg(0, std::ios::end);
		std::streampos file_size = stream.tellg();
		stream.seekg(0, std::ios::beg);
		std::string file_content(file_size, '\0');
		stream.read(file_content.data(), file_size);

		bool result = preprocessFileContent(file_content);
		
		// Restore the previous file index and parent line when returning
		current_file_index_ = saved_file_index;
		current_parent_line_ = saved_parent_line;
		
		return result;
	}

	bool preprocessFileContent(const std::string& file_content) {
		std::istringstream stream(file_content);
		std::string line;
		std::string pending_line;  // Line that was read but needs to be processed on next iteration
		bool has_pending_line = false;
		bool in_comment = false;
		std::stack<bool> skipping_stack;
		skipping_stack.push(false); // Initial state: not skipping
		// Track whether any condition in an #if/#elif chain has been true
		// This is needed for proper #elif handling
		std::stack<bool> condition_was_true_stack;
		condition_was_true_stack.push(false);

		long line_number_fallback = 1;
		long& line_number = !filestack_.empty() ? filestack_.top().line_number : line_number_fallback;
		long prev_line_number = -1;
		const bool isPreprocessorOnlyMode = settings_.isPreprocessorOnlyMode();
		size_t line_counter = 0;  // Add counter for debugging
		
		// Modified loop to handle pending lines
		auto getNextLine = [&]() -> bool {
			if (has_pending_line) {
				line = std::move(pending_line);
				has_pending_line = false;
				return true;
			}
			return static_cast<bool>(std::getline(stream, line));
		};
		
		while (getNextLine()) {
			line_counter++;
			if (settings_.isVerboseMode() && line_counter % 100 == 0) {
				std::cout << "Processing line " << line_counter << " in " << filestack_.top().file_name << std::endl;
			}
			size_t first_none_tab = line.find_first_not_of('\t');
			if (first_none_tab != std::string::npos && first_none_tab != 0)
				line.erase(line.begin(), line.begin() + first_none_tab);

			++line_number;

			if (isPreprocessorOnlyMode && prev_line_number != line_number - 1) {
				std::cout << "# " << line_number << " \"" << filestack_.top().file_name << "\"\n";
				prev_line_number = line_number;
			}
			else {
				++prev_line_number;
			}

			if (in_comment) {
				size_t end_comment_pos = line.find("*/");
				if (end_comment_pos != std::string::npos) {
					in_comment = false;
					line = line.substr(end_comment_pos + 2);
				}
				else {
					continue;
				}
			}

			size_t start_comment_pos = line.find("/*");
			if (start_comment_pos != std::string::npos) {
				size_t end_comment_pos = line.find("*/", start_comment_pos);
				if (end_comment_pos != std::string::npos) {
					line.erase(start_comment_pos, end_comment_pos - start_comment_pos + 2);
				}
				else {
					in_comment = true;
					continue;
				}
			}

			if (skipping_stack.size() == 0) {
				FLASH_LOG(Lexer, Error, "Internal compiler error in file ", filestack_.top().file_name, ":", line_number,
				          " - preprocessor directive stack underflow (too many #endif directives or preprocessor state corruption). Line content: '", line, "'");
				return false;
			}
			const bool skipping = skipping_stack.top();

			// Find the position of the '#' character
			size_t directive_pos = line.find('#');
			if (directive_pos != std::string::npos) {
				size_t first_non_space = line.find_first_not_of(' ', 0);
				if (first_non_space > 0) {
					line.erase(0, first_non_space);
					directive_pos -= first_non_space;
				}
				// Find the position of the first non-whitespace character after the '#'
				size_t next_pos = find_first_non_whitespace_after_hash(line);
				if (next_pos != std::string::npos && (next_pos != directive_pos + 1)) {
					// Remove whitespaces between '#' and the directive
					line = line.substr(0, directive_pos + 1) + line.substr(next_pos);
				}

				size_t i;
				while (((i = line.rfind('\\')) != std::string::npos) && (i == line.size() - 1)) {
					std::string next_line;
					std::getline(stream, next_line);
					line.erase(line.end() - 1);
					line.append(next_line);
					++line_number;
				}
			}

			if (skipping) {
				if (line.find("#endif", 0) == 0) {
					FLASH_LOG(Lexer, Debug, "Preprocessor: #endif while skipping, stack size before pop: ", skipping_stack.size(), " at ", filestack_.top().file_name, ":", line_number);
					skipping_stack.pop();
					condition_was_true_stack.pop();
					FLASH_LOG(Lexer, Debug, "Preprocessor: stack size after pop: ", skipping_stack.size());
				}
				else if (line.find("#if", 0) == 0) {
					// Nesting: #if, #ifdef, #ifndef all start with "#if"
					// Push a new skipping state for any nested conditional
					// Mark condition_was_true as true to prevent #else/#elif from activating
					// (since we're skipping due to an outer condition, not this one)
					FLASH_LOG(Lexer, Debug, "Preprocessor: #if while skipping, pushing, stack size: ", skipping_stack.size(), " -> ", skipping_stack.size()+1, " at ", filestack_.top().file_name, ":", line_number);
					skipping_stack.push(true);
					condition_was_true_stack.push(true);  // Changed from false
				}
				else if (line.find("#elif", 0) == 0) {
					// If we're skipping and haven't found a true condition yet, evaluate #elif
					if (!condition_was_true_stack.top()) {
						std::string condition = line.substr(5);  // Skip "#elif"
						// Strip comments from the condition before evaluating
						// Handle // line comments
						size_t line_comment_pos = condition.find("//");
						if (line_comment_pos != std::string::npos) {
							condition = condition.substr(0, line_comment_pos);
						}
						// Handle /* */ block comments
						size_t block_start = condition.find("/*");
						while (block_start != std::string::npos) {
							size_t block_end = condition.find("*/", block_start + 2);
							if (block_end != std::string::npos) {
								condition.erase(block_start, block_end - block_start + 2);
							} else {
								// Unterminated block comment - remove to end and log warning
								FLASH_LOG(Lexer, Warning, "Unterminated block comment in #elif condition at ",
								          filestack_.top().file_name, ":", filestack_.top().line_number);
								condition = condition.substr(0, block_start);
								break;
							}
							block_start = condition.find("/*");
						}
						// Trim trailing whitespace
						size_t end_pos = condition.find_last_not_of(" \t\r\n");
						if (end_pos != std::string::npos) {
							condition = condition.substr(0, end_pos + 1);
						} else {
							// Condition is empty or all whitespace - treat as 0
							condition.clear();
						}
						condition = expandMacrosForConditional(condition);
						std::istringstream iss(condition);
						long expression_result = evaluate_expression(iss);
						if (expression_result != 0) {
							skipping_stack.top() = false;  // Stop skipping
							condition_was_true_stack.top() = true;  // Mark that we found a true condition
						}
					}
					// If a previous condition was true, keep skipping
				}
				else if (line.find("#else", 0) == 0) {
					// Only stop skipping if no previous condition was true
					if (!condition_was_true_stack.top()) {
						skipping_stack.top() = false;
						condition_was_true_stack.top() = true;
					}
				}
				continue;
			}

			size_t comment_pos = line.find("//");
			if (comment_pos != std::string::npos) {
				if (comment_pos == 0) {
					continue;
				}
				line = line.substr(0, comment_pos);
			}

			if (line.find("#include_next", 0) == 0) {
				// GCC extension: #include_next searches for the header starting
				// from the directory AFTER the one where the current file was found.
				// Used by C++ standard library headers to include underlying C headers.
				// Must be checked before #include since #include_next starts with #include.
				append_line_with_tracking("// " + line);
				
				if (!processIncludeNextDirective(line, filestack_.top().file_name, line_number)) {
					return false;
				}
				prev_line_number = 0;
			}
			else if (line.find("#include", 0) == 0) {
				// Record the #include line in line_map BEFORE processing it
				// so that the included file can find its parent
				append_line_with_tracking("// " + line);  // Comment it out in output
				
				if (!processIncludeDirective(line, filestack_.top().file_name, line_number)) {
					return false;
				}
				// Reset prev_line_number so we print the next row
				prev_line_number = 0;
			}
			else if (line.find("#define", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#define"sv.length());
				handleDefine(iss);
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#ifdef", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#ifdef"sv.length());
				std::string symbol;
				iss >> symbol;
				// __has_builtin is a compiler intrinsic - standard library uses "#ifdef __has_builtin"
				// to check if the feature is available, then uses __has_builtin(x) with arguments.
				// We return true for "#ifdef __has_builtin" so the library defines _GLIBCXX_HAS_BUILTIN.
				bool is_defined = (symbol == "__has_builtin") || (defines_.count(symbol) > 0);
				FLASH_LOG(Lexer, Debug, "Preprocessor: #ifdef ", symbol, " (defined=", is_defined, "), pushing, stack size: ", skipping_stack.size(), " -> ", skipping_stack.size()+1, " at ", filestack_.top().file_name, ":", line_number);
				skipping_stack.push(!is_defined);
				condition_was_true_stack.push(is_defined);
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#ifndef", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#ifndef"sv.length());
				std::string symbol;
				iss >> symbol;
				bool is_defined = defines_.count(symbol) > 0;
				FLASH_LOG(Lexer, Debug, "Preprocessor: #ifndef ", symbol, " (defined=", is_defined, "), pushing, stack size: ", skipping_stack.size(), " -> ", skipping_stack.size()+1, " at ", filestack_.top().file_name, ":", line_number);
				skipping_stack.push(is_defined);
				condition_was_true_stack.push(!is_defined);
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#if", 0) == 0) {
				// Extract and expand macros in the condition before evaluation
				std::string condition = line.substr(3);  // Skip "#if"
				condition = expandMacrosForConditional(condition);
				std::istringstream iss(condition);
				long expression_result = evaluate_expression(iss);
				bool condition_true = (expression_result != 0);
				FLASH_LOG(Lexer, Debug, "Preprocessor: #if (result=", condition_true, "), pushing, stack size: ", skipping_stack.size(), " -> ", skipping_stack.size()+1, " at ", filestack_.top().file_name, ":", line_number);
				skipping_stack.push(!condition_true);
				condition_was_true_stack.push(condition_true);
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#elif", 0) == 0) {
				if (skipping_stack.empty() || condition_was_true_stack.empty()) {
					FLASH_LOG(Lexer, Error, "Unmatched #elif directive");
					return false;
				}
				// If a previous condition was true, start skipping
				if (condition_was_true_stack.top()) {
					skipping_stack.top() = true;
				}
				else {
					// Evaluate the #elif condition
					std::string condition = line.substr(5);  // Skip "#elif"
					condition = expandMacrosForConditional(condition);
					std::istringstream iss(condition);
					long expression_result = evaluate_expression(iss);
					if (expression_result != 0) {
						skipping_stack.top() = false;  // Stop skipping
						condition_was_true_stack.top() = true;  // Mark condition as true
					}
				}
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#else", 0) == 0) {
				if (skipping_stack.empty() || condition_was_true_stack.empty()) {
					FLASH_LOG(Lexer, Error, "Unmatched #else directive");
					return false;
				}
				// Only execute #else block if no previous condition was true
				if (condition_was_true_stack.top()) {
					skipping_stack.top() = true;  // Start skipping
				}
				else {
					skipping_stack.top() = false;  // Stop skipping
					condition_was_true_stack.top() = true;  // Mark that we're in a true block
				}
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#endif", 0) == 0) {
				if (!skipping_stack.empty()) {
					FLASH_LOG(Lexer, Debug, "Preprocessor: #endif (not skipping), stack size before pop: ", skipping_stack.size(), " at ", filestack_.top().file_name, ":", line_number);
					skipping_stack.pop();
					condition_was_true_stack.pop();
					FLASH_LOG(Lexer, Debug, "Preprocessor: stack size after pop: ", skipping_stack.size());
				}
				else {
					FLASH_LOG(Lexer, Error, "Unmatched #endif directive");
					return false;
				}
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#error", 0) == 0) {
				std::string message = line.substr(6);
				// Trim leading whitespace
				size_t first_non_space = message.find_first_not_of(" \t");
				if (first_non_space != std::string::npos) {
					message = message.substr(first_non_space);
				}
				FLASH_LOG(Lexer, Error, message);
				return false;
			}
			else if (line.find("#warning", 0) == 0) {
				std::string message = line.substr(8);
				// Trim leading whitespace
				size_t first_non_space = message.find_first_not_of(" \t");
				if (first_non_space != std::string::npos) {
					message = message.substr(first_non_space);
				}
				FLASH_LOG(Lexer, Warning, message);
			}
			else if (line.find("#undef") == 0) {
				std::istringstream iss(line);
				iss.seekg("#undef"sv.length());
				std::string symbol;
				iss >> symbol;
				defines_.erase(symbol);
				append_line_with_tracking("");  // Preserve line numbering
			}
			else if (line.find("#pragma once", 0) == 0) {
				proccessedHeaders_.insert(std::string(filestack_.top().file_name));
			}
			else if (line.find("#pragma pack", 0) == 0) {
				processPragmaPack(line);
				// Pass through the pragma pack directive to the parser
				append_line_with_tracking(line);
			}
			else if (line.find("#pragma", 0) == 0) {
				// Skip other #pragma directives (like #pragma GCC visibility)
				// These are compiler-specific and don't need to be passed to the parser
				// Just output a blank line to preserve line numbers
				append_line_with_tracking("");
			}
			else if (line.find("#line", 0) == 0) {
				processLineDirective(line);
			}
			else {
				// Handle multiline macro invocations.
				// If a line has an incomplete macro invocation (unmatched parens),
				// keep reading lines until we have matching parens.
				// Also handles function-like macro names on one line with '(' on the next.
				// BUT: Stop if the next line is a preprocessor directive (starts with #)
				// because preprocessor directives cannot be inside macro invocations.
				auto mergeNextLine = [&]() -> bool {
					std::string next_line;
					if (!std::getline(stream, next_line)) return false;
					++line_number;
					
					size_t first_non_ws = next_line.find_first_not_of(" \t");
					if (first_non_ws != std::string::npos && next_line[first_non_ws] == '#') {
						pending_line = std::move(next_line);
						has_pending_line = true;
						--line_number;
						return false;
					}
					
					line += " " + next_line;
					return true;
				};
				while (hasIncompleteMacroInvocation(line)) {
					if (!mergeNextLine()) break;
				}
				// C standard §6.10.3: If a function-like macro name is NOT followed
				// by '(' it is not a macro invocation. When the '(' appears on the
				// next line we must merge those lines before expansion.
				if (!hasIncompleteMacroInvocation(line)) {
					// Extract trailing identifier and check if it's a function-like macro
					size_t end = line.size();
					while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1])))
						--end;
					size_t id_end = end;
					while (end > 0 && (std::isalnum(static_cast<unsigned char>(line[end - 1])) || line[end - 1] == '_'))
						--end;
					if (end < id_end) {
						std::string trailing_id(line, end, id_end - end);
						auto it = defines_.find(trailing_id);
						if (it != defines_.end()) {
							auto* dd = it->second.get_if<DefineDirective>();
							if (dd && dd->is_function_like) {
								// Peek at the next line to see if it starts with '('
								std::string peek_line;
								bool got_peek = false;
								if (has_pending_line) {
									peek_line = pending_line;
									got_peek = true;
								} else if (std::getline(stream, peek_line)) {
									++line_number;
									got_peek = true;
								}
								if (got_peek) {
									size_t fw = peek_line.find_first_not_of(" \t");
									if (fw != std::string::npos && peek_line[fw] == '(') {
										// Merge: the next line continues this macro invocation
										if (has_pending_line)
											has_pending_line = false;
										line += " " + peek_line;
										// Continue merging if parens are still unmatched
										while (hasIncompleteMacroInvocation(line)) {
											if (!mergeNextLine()) break;
										}
									} else {
										// Not a macro invocation; push back the peeked line
										if (!has_pending_line) {
											pending_line = std::move(peek_line);
											has_pending_line = true;
											--line_number;
										}
									}
								}
							}
						}
					}
				}
				
				// Expand macros in non-directive lines (regular source code).
				// Only expand if the line is non-empty to avoid unnecessary processing.
				if (line.size() > 0)
					line = expandMacros(line);

				if (isPreprocessorOnlyMode) {
					std::cout << line << "\n";
				}

				append_line_with_tracking(line);
			}
		}

		return true;
	}

	void push_file_to_stack(const CurrentFile& current_file) {
		filestack_.emplace(current_file);
	}

	const std::string& get_result() const {
		return result_;
	}
	
	// Append a line to the result and record its source location
	void append_line_with_tracking(const std::string& line) {
		// Record the mapping before appending
		if (!filestack_.empty()) {
			const auto& current_file = filestack_.top();
			
			line_map_.push_back({
				current_file_index_,  // Current file
				static_cast<size_t>(current_file.line_number),  // Line in current file
				current_parent_line_  // Preprocessed line where current file was #included
			});
		}
		
		result_.append(line).append("\n");
		current_output_line_++;
	}
	
	// Get the index of a file path (must already exist)
	size_t get_file_path_index(std::string_view file_path) const {
		auto it = std::find(file_paths_.begin(), file_paths_.end(), file_path);
		if (it != file_paths_.end()) {
			return std::distance(file_paths_.begin(), it);
		}
		// Should never happen if readFile() properly registers files
		return 0;
	}
	
	// Add a file path if it doesn't already exist, return its index
	size_t get_or_add_file_path(std::string_view file_path) {
		auto it = std::find(file_paths_.begin(), file_paths_.end(), file_path);
		if (it != file_paths_.end()) {
			return std::distance(file_paths_.begin(), it);
		}
		size_t new_index = file_paths_.size();
		file_paths_.push_back(std::string(file_path));
		return new_index;
	}

private:
	// Separator bitset - 32 bytes (4 × uint64_t), one bit per character
	// Generated at compile-time by looping over separator_chars array
	static constexpr std::array<uint64_t, 4> separator_bitset = {
		build_separator_bitset_chunk(0),  // Chars 0-63
		build_separator_bitset_chunk(1),  // Chars 64-127
		build_separator_bitset_chunk(2),  // Chars 128-191
		build_separator_bitset_chunk(3)   // Chars 192-255
	};

	static constexpr bool is_seperator_character(char c) {
		unsigned char uc = static_cast<unsigned char>(c);
		return (separator_bitset[uc >> 6] >> (uc & 0x3F)) & 1;
	}
	// Helper function to check if a position is inside a string literal (static version)
	// Handles both regular strings "..." and raw strings R"(...)" or R"delim(...)delim"
	static bool is_inside_string_literal(const std::string& str, size_t pos) {
		bool inside_string = false;
		bool inside_raw_string = false;
		bool escaped = false;
		std::string_view raw_delimiter;

		for (size_t i = 0; i < pos && i < str.size(); ++i) {
			if (inside_raw_string) {
				// Inside a raw string - look for )" followed by delimiter and "
				if (str[i] == ')' && i + 1 + raw_delimiter.size() < str.size()) {
					if (str[i + 1] == '"') {
						// Check if delimiter matches (empty delimiter case)
						if (raw_delimiter.empty()) {
							inside_raw_string = false;
							i += 1; // Skip the closing "
							continue;
						}
					} else if (i + 1 + raw_delimiter.size() < str.size()) {
						// Check if delimiter matches
						bool delimiter_matches = true;
						for (size_t j = 0; j < raw_delimiter.size(); ++j) {
							if (str[i + 1 + j] != raw_delimiter[j]) {
								delimiter_matches = false;
								break;
							}
						}
						if (delimiter_matches && str[i + 1 + raw_delimiter.size()] == '"') {
							inside_raw_string = false;
							i += raw_delimiter.size() + 1; // Skip delimiter and closing "
							continue;
						}
					}
				}
			} else if (inside_string) {
				// Inside a regular string
				if (escaped) {
					escaped = false;
					continue;
				}

				if (str[i] == '\\') {
					escaped = true;
				} else if (str[i] == '"') {
					inside_string = false;
				}
			} else {
				// Outside any string - check for string start
				// Check for raw string literal: R"delim(
				if (str[i] == 'R' && i + 2 < str.size() && str[i + 1] == '"') {
					inside_raw_string = true;
					raw_delimiter = std::string_view{};
					i += 2; // Skip R"

					// Extract delimiter (characters between " and ()
					while (i < str.size() && str[i] != '(') {
						++i;
					}
					raw_delimiter = std::string_view(str.data() + 2, i - 2);
					// i now points to '(', continue from next character
					continue;
				}
				// Check for regular string literal
				else if (str[i] == '"') {
					inside_string = true;
				}
			}
		}

		return inside_string || inside_raw_string;
	}

	// Expand macros for #if/#elif expressions, preserving identifiers inside defined()
	std::string expandMacrosForConditional(const std::string& input) {
		if (settings_.isVerboseMode()) {
			FLASH_LOG(Lexer, Trace, "expandMacrosForConditional input: '", input, "'");
		}
		
		std::string result;
		size_t pos = 0;
		
		while (pos < input.size()) {
			// Look for "defined" or "__has_builtin" keywords
			size_t defined_pos = input.find("defined", pos);
			size_t has_builtin_pos = input.find("__has_builtin", pos);
			
			// Choose the earlier one
			size_t keyword_pos = std::string::npos;
			size_t keyword_len = 0;
			bool is_has_builtin = false;
			
			if (defined_pos != std::string::npos && (has_builtin_pos == std::string::npos || defined_pos < has_builtin_pos)) {
				keyword_pos = defined_pos;
				keyword_len = 7; // "defined"
			} else if (has_builtin_pos != std::string::npos) {
				keyword_pos = has_builtin_pos;
				keyword_len = 13; // "__has_builtin"
				is_has_builtin = true;
			}
			
			if (keyword_pos == std::string::npos) {
				// No more special keywords, expand the rest
				if (settings_.isVerboseMode() && pos < input.size()) {
					FLASH_LOG(Lexer, Trace, "  Expanding rest: '", input.substr(pos), "'");
				}
				result += expandMacros(input.substr(pos));
				break;
			}
			
			// Check if this is actually a keyword (not part of another identifier)
			bool is_keyword = true;
			if (keyword_pos > 0) {
				char prev = input[keyword_pos - 1];
				if (std::isalnum(prev) || prev == '_') {
					is_keyword = false;
				}
			}
			if (is_keyword && keyword_pos + keyword_len < input.size()) {
				char next = input[keyword_pos + keyword_len];
				if (std::isalnum(next) || next == '_') {
					is_keyword = false;
				}
			}
			
			if (!is_keyword) {
				// Not actually a keyword, just expand and continue
				result += expandMacros(input.substr(pos, keyword_pos - pos + 1));
				pos = keyword_pos + 1;
				continue;
			}
			
			// Expand everything before the keyword
			if (settings_.isVerboseMode() && keyword_pos > pos) {
				FLASH_LOG(Lexer, Trace, "  Expanding before keyword: '", input.substr(pos, keyword_pos - pos), "'");
			}
			result += expandMacros(input.substr(pos, keyword_pos - pos));
			
			// For __has_builtin, copy the entire __has_builtin(...) expression without expansion
			if (is_has_builtin) {
				result += "__has_builtin";
				pos = keyword_pos + keyword_len;
				
				// Skip whitespace
				while (pos < input.size() && std::isspace(input[pos])) {
					result += input[pos++];
				}
				
				// Expect '(' for __has_builtin
				if (pos < input.size() && input[pos] == '(') {
					result += "(";
					pos++;
					
					// Find matching ')'
					int paren_depth = 1;
					while (pos < input.size() && paren_depth > 0) {
						if (input[pos] == '(') paren_depth++;
						else if (input[pos] == ')') paren_depth--;
						result += input[pos++];
					}
				}
				continue;
			}
			
			// For "defined" keyword, use existing logic
			result += "defined";  // Add the keyword itself
			
			// Skip past "defined"
			pos = keyword_pos + 7; // length of "defined"
			
			// Skip whitespace
			while (pos < input.size() && std::isspace(input[pos])) {
				result += input[pos++];
			}
			
			// Check if there's a '('
			bool has_paren = (pos < input.size() && input[pos] == '(');
			if (has_paren) {
				result += "(";
				pos++;
				// Skip whitespace after '('
				while (pos < input.size() && std::isspace(input[pos])) {
					result += input[pos++];
				}
			}
			
			// Extract the identifier (don't expand it!)
			size_t ident_start = pos;
			while (pos < input.size() && (std::isalnum(input[pos]) || input[pos] == '_')) {
				pos++;
			}
			result += input.substr(ident_start, pos - ident_start);
			
			// Skip whitespace
			while (pos < input.size() && std::isspace(input[pos])) {
				result += input[pos++];
			}
			
			// If there was a '(', expect a ')'
			if (has_paren && pos < input.size() && input[pos] == ')') {
				result += ")";
				pos++;
			}
		}
		
		if (settings_.isVerboseMode()) {
			FLASH_LOG(Lexer, Trace, "expandMacrosForConditional output: '", result, "'");
		}
		
		return result;
	}

	// Optimized single-pass macro expansion (C++20 compliant)
	// Instead of searching for all ~200 defines at every position (O(D*N*M)),
	// we scan the input once, extract identifiers, and look them up in the hash table (O(N*avgIdLen))
	//
	// C++ preprocessing rules observed:
	// 1. Macro arguments are fully expanded before substitution (except for # and ##)
	// 2. A function-like macro is only invoked if followed by '(' (whitespace allowed between)
	// 3. Recursive expansion is prevented by tracking currently-expanding macros
	// 4. Token pasting (##) is performed after argument substitution
	// 5. Stringification (#) uses unexpanded argument text
