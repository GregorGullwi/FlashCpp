	std::string expandMacros(const std::string& input, std::unordered_set<std::string> expanding_macros = {}) {
		// Check if we're inside a multiline raw string from a previous line
		if (inside_multiline_raw_string_) {
			std::string closing = ")" + multiline_raw_delimiter_ + "\"";
			size_t close_pos = input.find(closing);
			if (close_pos != std::string::npos) {
				inside_multiline_raw_string_ = false;
				multiline_raw_delimiter_.clear();
			}
			return input;
		}

		// Check for multiline raw string start
		size_t raw_start = 0;
		while ((raw_start = input.find("R\"", raw_start)) != std::string::npos) {
			size_t delim_start = raw_start + 2;
			size_t paren_pos = input.find('(', delim_start);
			if (paren_pos != std::string::npos && paren_pos <= delim_start + 16) { // max delimiter length
				std::string delimiter = input.substr(delim_start, paren_pos - delim_start);
				std::string closing = ")" + delimiter + "\"";
				if (input.find(closing, paren_pos) == std::string::npos) {
					inside_multiline_raw_string_ = true;
					multiline_raw_delimiter_ = delimiter;
					return input;
				}
			}
			raw_start++;
		}

		// Use a working copy to avoid const_cast issues
		std::string current = input;
		std::string output;
		output.reserve(current.size() * 2);  // Reserve space to avoid reallocations
		
		size_t loop_guard = 1000;  // Safety limit for expansion iterations
		bool needs_another_pass = true;  // Controls iteration - start with true to do at least one pass
		
		// We need to iterate because expansions can introduce new macros
		// Each pass scans the entire string for macros to expand
		while (needs_another_pass && loop_guard-- > 0) {
			needs_another_pass = false;  // Will be set true if we expand any macros
			output.clear();
			
			size_t pos = 0;
			size_t input_size = current.size();
			bool in_string = false;
			bool in_char = false;
			bool in_raw_string = false;
			std::string raw_delimiter;
			
			while (pos < input_size) {
				char c = current[pos];
				
				// Handle escape sequences in strings
				if ((in_string || in_char) && c == '\\' && pos + 1 < input_size) {
					output += c;
					output += current[++pos];
					++pos;
					continue;
				}
				
				// Handle raw string literals
				if (!in_string && !in_char && !in_raw_string && c == 'R' && pos + 1 < input_size && current[pos + 1] == '"') {
					// Start of raw string - find delimiter
					size_t delim_start = pos + 2;
					size_t paren = current.find('(', delim_start);
					if (paren != std::string::npos && paren <= delim_start + 16) {
						raw_delimiter = current.substr(delim_start, paren - delim_start);
						in_raw_string = true;
						// Copy up to and including the opening paren
						while (pos <= paren) {
							output += current[pos++];
						}
						continue;
					}
				}
				
				if (in_raw_string) {
					// Look for closing delimiter
					std::string closing = ")" + raw_delimiter + "\"";
					if (pos + closing.size() <= input_size && 
					    current.compare(pos, closing.size(), closing) == 0) {
						// Found closing, copy it and exit raw string mode
						output += current.substr(pos, closing.size());
						pos += closing.size();
						in_raw_string = false;
						raw_delimiter.clear();
						continue;
					}
					output += c;
					++pos;
					continue;
				}
				
				// Handle regular string literals
				if (!in_char && c == '"') {
					in_string = !in_string;
					output += c;
					++pos;
					continue;
				}
				
				// Handle character literals
				if (!in_string && c == '\'') {
					in_char = !in_char;
					output += c;
					++pos;
					continue;
				}
				
				// Inside string/char literal, just copy
				if (in_string || in_char) {
					output += c;
					++pos;
					continue;
				}
				
				// Check for identifier start (letter or underscore)
				if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
					size_t ident_start = pos;
					++pos;
					// Consume the rest of the identifier
					while (pos < input_size && (std::isalnum(static_cast<unsigned char>(current[pos])) || current[pos] == '_')) {
						++pos;
					}
					
					std::string_view ident(current.data() + ident_start, pos - ident_start);
					std::string ident_str(ident);
					
					// Skip if this macro is being expanded (prevent recursion per C++ standard)
					if (expanding_macros.count(ident_str) > 0) {
						output += ident;
						continue;
					}
					
					// Look up identifier in defines
					auto it = defines_.find(ident_str);
					if (it != defines_.end()) {
						const Directive& directive = it->second;
						std::string replace_str;
						
						if (auto* defineDirective = directive.get_if<DefineDirective>()) {
							replace_str = defineDirective->body;
							
							// Use the is_function_like flag to properly detect function-like macros
							// This handles variadic macros like #define FOO(...) which have empty args but are function-like
							if (defineDirective->is_function_like) {
								// Look for opening parenthesis (skip whitespace)
								size_t paren_pos = pos;
								while (paren_pos < input_size && std::isspace(static_cast<unsigned char>(current[paren_pos]))) {
									++paren_pos;
								}
								
								if (paren_pos >= input_size || current[paren_pos] != '(') {
									// No '(' found - this is not a macro invocation, just copy the identifier
									output += ident;
									continue;
								}
								
								size_t args_start = paren_pos;
								size_t args_end = findMatchingClosingParen(current, args_start);
								if (args_end == std::string::npos) {
									// Malformed - no closing paren
									output += ident;
									continue;
								}
								
								std::vector<std::string_view> args = splitArgs(
									std::string_view(current).substr(args_start + 1, args_end - args_start - 1));
								
								// Handle variadic arguments (__VA_ARGS__)
								std::string va_args_str;
								bool has_variadic_args = false;
								if (args.size() > defineDirective->args.size()) {
									has_variadic_args = true;
									for (size_t i = defineDirective->args.size(); i < args.size(); ++i) {
										if (!va_args_str.empty()) {
											va_args_str += ", ";
										}
										va_args_str += args[i];
									}
								}
								
								// Handle __VA_OPT__ (C++20 feature)
								// __VA_OPT__(content) expands to content if __VA_ARGS__ is non-empty, otherwise empty
								size_t va_opt_pos = 0;
								while ((va_opt_pos = replace_str.find("__VA_OPT__", va_opt_pos)) != std::string::npos) {
									size_t opt_paren_start = replace_str.find('(', va_opt_pos + 10);
									if (opt_paren_start != std::string::npos) {
										size_t opt_paren_end = findMatchingClosingParen(replace_str, opt_paren_start);
										if (opt_paren_end != std::string::npos) {
											std::string opt_content = replace_str.substr(opt_paren_start + 1, opt_paren_end - opt_paren_start - 1);
											std::string replacement = has_variadic_args ? opt_content : "";
											replace_str.replace(va_opt_pos, opt_paren_end - va_opt_pos + 1, replacement);
											va_opt_pos += replacement.length();
										} else break;
									} else break;
								}
								
								// Handle __VA_ARGS__
								size_t va_args_pos = replace_str.find("__VA_ARGS__");
								if (va_args_pos != std::string::npos) {
									replace_str.replace(va_args_pos, 11, va_args_str);
								}
								
								// Substitute macro arguments
								if (args.size() < defineDirective->args.size()) {
									// Not enough arguments per C++ standard - skip expansion
									output += ident;
									continue;
								}
								
								// Per C++ standard 6.10.3.1: Determine which parameters are operands of # or ##
								// Arguments for such parameters must NOT be pre-expanded
								auto is_separator = [](char c) {
									return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
								};
								auto paramAdjacentToHashHash = [&](const std::string& param_name, const std::string& body) -> bool {
									size_t search_pos = 0;
									while (search_pos < body.size()) {
										size_t found = body.find(param_name, search_pos);
										if (found == std::string::npos) break;
										// Check if this is a whole-word match
										bool start_ok = (found == 0) || is_separator(body[found - 1]);
										bool end_ok = (found + param_name.size() >= body.size()) || is_separator(body[found + param_name.size()]);
										if (start_ok && end_ok) {
											// Check if preceded by ## (skip whitespace)
											size_t before = found;
											while (before > 0 && std::isspace(static_cast<unsigned char>(body[before - 1]))) --before;
											if (before >= 2 && body[before - 2] == '#' && body[before - 1] == '#')
												return true;
											// Check if followed by ## (skip whitespace)
											size_t after = found + param_name.size();
											while (after < body.size() && std::isspace(static_cast<unsigned char>(body[after]))) ++after;
											if (after + 1 < body.size() && body[after] == '#' && body[after + 1] == '#')
												return true;
										}
										search_pos = found + 1;
									}
									return false;
								};
								
								for (size_t i = 0; i < defineDirective->args.size(); ++i) {
									// Handle stringification (#) - uses UNEXPANDED argument per C++ standard
									size_t stringify_pos = 0;
									std::string search_str = "#" + defineDirective->args[i];
									while ((stringify_pos = replace_str.find(search_str, stringify_pos)) != std::string::npos) {
										// Skip if part of ## (token pasting)
										if (stringify_pos > 0 && replace_str[stringify_pos - 1] == '#') {
											stringify_pos++;
											continue;
										}
										size_t arg_end = stringify_pos + search_str.length();
										if (arg_end < replace_str.length() && replace_str[arg_end] == '#') {
											stringify_pos++;
											continue;
										}
										// Stringification uses unexpanded argument
										replace_str.replace(stringify_pos, search_str.length(), 
											std::format("\"{0}\"", args[i]));
										stringify_pos += args[i].length() + 2;
									}
									// Per C++ standard: arguments NOT adjacent to # or ## are expanded before substitution
									// Arguments adjacent to ## are substituted unexpanded (token pasting operates on raw tokens)
									std::string_view arg_value = args[i];
									std::string expanded_arg;
									if (!paramAdjacentToHashHash(defineDirective->args[i], defineDirective->body)) {
										expanded_arg = expandMacros(std::string(arg_value), expanding_macros);
										arg_value = expanded_arg;
									}
									replaceAll(replace_str, defineDirective->args[i], arg_value);
								}
								
								pos = args_end + 1;  // Move past the closing paren
							}
						} else if (auto* function_directive = directive.get_if<FunctionDirective>()) {
							replace_str = function_directive->getBody();
						}
						
						// Per C standard 6.10.3.3: Process ## token-pasting BEFORE rescanning
						// This must happen after argument substitution but before macro expansion
						{
							size_t pp = 0;
							while ((pp = replace_str.find("##", pp)) != std::string::npos) {
								size_t wb = pp;
								while (wb > 0 && std::isspace(static_cast<unsigned char>(replace_str[wb - 1]))) --wb;
								size_t wa = pp + 2;
								while (wa < replace_str.size() && std::isspace(static_cast<unsigned char>(replace_str[wa]))) ++wa;
								
								// Before concatenating, expand predefined macros (FunctionDirectives like
								// __COUNTER__, __LINE__, etc.) on either side of ##. These are not macro
								// arguments, so the "don't expand adjacent to ##" rule doesn't apply to them.
								// Extract the right-side token (identifier) and expand if it's a FunctionDirective
								if (wa < replace_str.size() && (std::isalpha(static_cast<unsigned char>(replace_str[wa])) || replace_str[wa] == '_')) {
									size_t token_end = wa;
									while (token_end < replace_str.size() && (std::isalnum(static_cast<unsigned char>(replace_str[token_end])) || replace_str[token_end] == '_'))
										++token_end;
									std::string right_token = replace_str.substr(wa, token_end - wa);
									auto it = defines_.find(right_token);
									if (it != defines_.end()) {
										if (auto* func_dir = it->second.get_if<FunctionDirective>()) {
											std::string expanded = func_dir->getBody();
											replace_str = replace_str.substr(0, wa) + expanded + replace_str.substr(token_end);
											// wa stays the same, but the token is now the expanded value
										}
									}
								}
								// Extract the left-side token (identifier) and expand if it's a FunctionDirective
								if (wb > 0) {
									size_t token_start = wb;
									while (token_start > 0 && (std::isalnum(static_cast<unsigned char>(replace_str[token_start - 1])) || replace_str[token_start - 1] == '_'))
										--token_start;
									if (token_start < wb) {
										std::string left_token = replace_str.substr(token_start, wb - token_start);
										auto it = defines_.find(left_token);
										if (it != defines_.end()) {
											if (auto* func_dir = it->second.get_if<FunctionDirective>()) {
												std::string expanded = func_dir->getBody();
												// Replace the left token with its expansion, adjusting positions
												size_t old_len = wb - token_start;
												replace_str = replace_str.substr(0, token_start) + expanded + replace_str.substr(wb);
												// Adjust pp and wa for the length change
												int delta = static_cast<int>(expanded.size()) - static_cast<int>(old_len);
												pp += delta;
												wb = token_start + expanded.size();
												wa += delta;
											}
										}
									}
								}
								
								replace_str = replace_str.substr(0, wb) + replace_str.substr(wa);
								pp = wb;  // Continue scanning from the paste point
							}
						}

						// Recursively expand the replacement (with this macro marked as expanding)
						auto new_expanding = expanding_macros;
						new_expanding.insert(ident_str);
						replace_str = expandMacros(replace_str, new_expanding);
						
						output += replace_str;
						needs_another_pass = true;  // We expanded something, may need another pass
					} else {
						// Not a macro, copy the identifier as-is
						output += ident;
					}
				} else {
					// Not an identifier, just copy
					output += c;
					++pos;
				}
			}
			
			// Prepare for next iteration if we had expansions
			if (needs_another_pass) {
				current = std::move(output);
				output.clear();
				output.reserve(current.size() * 2);
			}
		}
		
		if (loop_guard == 0) {
			FLASH_LOG(Lexer, Warning, "Macro expansion limit reached for line (possible infinite recursion): ", input.substr(0, 100));
		}
		
		// The final result is in 'output' if we completed a full pass without expansions,
		// or in 'current' if we hit the loop guard during a pass with expansions
		std::string& result = needs_another_pass ? current : output;
		
		// Handle token-pasting operator (##) - done after all substitutions per C++ standard
		size_t paste_pos;
		while ((paste_pos = result.find("##")) != std::string::npos) {
			size_t ws_before = paste_pos;
			while (ws_before > 0 && std::isspace(static_cast<unsigned char>(result[ws_before - 1]))) {
				--ws_before;
			}
			size_t ws_after = paste_pos + 2;
			while (ws_after < result.size() && std::isspace(static_cast<unsigned char>(result[ws_after]))) {
				++ws_after;
			}
			result = result.substr(0, ws_before) + result.substr(ws_after);
		}

		return result;
	}

	void apply_operator(std::stack<long>& values, std::stack<Operator>& ops) {
		if (ops.empty() || values.size() < 1) {
			if (settings_.isVerboseMode()) {
				FLASH_LOG(Lexer, Trace, "apply_operator: ops.empty()=", ops.empty(), 
				          " values.size()=", values.size());
			}
			FLASH_LOG(Lexer, Error, "Internal compiler error, values don't match the ops!");
			return;
		}

		Operator op = ops.top();
		if (settings_.isVerboseMode()) {
			FLASH_LOG(Lexer, Trace, "Applying operator (values.size=", values.size(), ")");
		}
		
		if (op == Operator::OpenParen) {
			ops.pop();
			return;
		}

		// Unary operators
		if (op == Operator::Not) {
			auto value = values.top();
			values.pop();
			values.push(!value);
		}
		else if (op == Operator::BitwiseNot) {
			auto value = values.top();
			values.pop();
			values.push(~value);
		}
		else if (values.size() >= 2) {
			auto right = values.top();
			values.pop();
			auto left = values.top();
			values.pop();

			switch (op) {
			case Operator::And:
				values.push(left && right);
				break;
			case Operator::Or:
				values.push(left || right);
				break;
			case Operator::Less:
				values.push(left < right);
				break;
			case Operator::Greater:
				values.push(left > right);
				break;
			case Operator::Equals:
				values.push(left == right);
				break;
			case Operator::NotEquals:
				values.push(left != right);
				break;
			case Operator::LessEquals:
				values.push(left <= right);
				break;
			case Operator::GreaterEquals:
				values.push(left >= right);
				break;
			// Arithmetic operators
			case Operator::Add:
				values.push(left + right);
				break;
			case Operator::Subtract:
				values.push(left - right);
				break;
			case Operator::Multiply:
				values.push(left * right);
				break;
			case Operator::Divide:
				if (right != 0) {
					values.push(left / right);
				} else {
					if (!filestack_.empty()) {
						FLASH_LOG(Lexer, Warning, "Division by zero in preprocessor expression (", left, " / 0) at ",
						          filestack_.top().file_name, ":", filestack_.top().line_number);
					} else {
						FLASH_LOG(Lexer, Warning, "Division by zero in preprocessor expression (", left, " / 0)");
					}
					values.push(0);
				}
				break;
			case Operator::Modulo:
				if (right != 0) {
					values.push(left % right);
				} else {
					if (!filestack_.empty()) {
						FLASH_LOG(Lexer, Warning, "Modulo by zero in preprocessor expression (", left, " % 0) at ",
						          filestack_.top().file_name, ":", filestack_.top().line_number);
					} else {
						FLASH_LOG(Lexer, Warning, "Modulo by zero in preprocessor expression (", left, " % 0)");
					}
					values.push(0);
				}
				break;
			// Bitwise operators
			case Operator::LeftShift:
				values.push(left << right);
				break;
			case Operator::RightShift:
				values.push(left >> right);
				break;
			case Operator::BitwiseAnd:
				values.push(left & right);
				break;
			case Operator::BitwiseOr:
				values.push(left | right);
				break;
			case Operator::BitwiseXor:
				values.push(left ^ right);
				break;
			default:
				FLASH_LOG(Lexer, Error, "Internal compiler error, unknown operator!");
				break;
			}
		}

		ops.pop();
	}

	bool parseIntegerLiteral(std::istringstream& iss, long& value, std::string* out_literal = nullptr) {
		std::string literal;
		iss >> std::ws;  // Skip leading whitespace
		int base = 10;

		if (iss.peek() == '0') {
			iss.get();
			char next = iss.peek();
			if (next == 'x' || next == 'X') {
				base = 16;
				iss.get();
			} else if (next == 'b' || next == 'B') {
				base = 2;
				iss.get();
			} else {
				base = 8;
				literal.push_back('0');
			}
		}

		auto is_digit_char = [&](char c) {
			if (c == '\'') return true; // digit separator
			switch (base) {
				case 2:  return c == '0' || c == '1';
				case 8:  return c >= '0' && c <= '7';
				case 10: return static_cast<bool>(std::isdigit(static_cast<unsigned char>(c)));
				case 16: return static_cast<bool>(std::isxdigit(static_cast<unsigned char>(c)));
				default: return false;
			}
		};

		while (iss && is_digit_char(iss.peek())) {
			if (iss.peek() == '\'') {
				iss.get();
				continue;
			}
			literal += iss.get();
		}

		if (literal.empty())
			return false;

		while (iss && (iss.peek()=='u'||iss.peek()=='U'||iss.peek()=='l'||iss.peek()=='L'))
			iss.get();

		auto [ptr, ec] = std::from_chars(literal.data(),
										 literal.data()+literal.size(),
										 value,
										 base);

		if (out_literal) *out_literal = literal;
		return ec == std::errc() &&
			   ptr == literal.data()+literal.size();
	}

	long evaluate_expression(std::istringstream& iss) {
		if (settings_.isVerboseMode()) {
			// Save position and read entire expression for debug
			auto pos = iss.tellg();
			std::string debug_expr;
			std::getline(iss, debug_expr);
			iss.clear();
			iss.seekg(pos);
			FLASH_LOG(Lexer, Trace, "Evaluating expression: '", debug_expr, "'");
		}
		
		// Check if expression is empty (all whitespace) - treat as 0
		auto start_pos = iss.tellg();
		iss >> std::ws;  // Skip whitespace
		if (iss.eof() || iss.peek() == EOF) {
			if (settings_.isVerboseMode()) {
				FLASH_LOG(Lexer, Trace, "  Empty expression, returning 0");
			}
			return 0;
		}
		iss.seekg(start_pos);  // Reset to start
		
		std::stack<long> values;
		std::stack<Operator> ops;

		std::string op_str;
		size_t eval_loop_guard = 10000;  // Add loop guard

		while (iss && eval_loop_guard-- > 0) {
			char c = iss.peek();
			if (isdigit(c)) {
				long value = 0;
				std::string literal;
				if (!parseIntegerLiteral(iss, value, &literal)) {
					FLASH_LOG_FORMAT(Lexer, Error, "Failed to parse integer literal '", literal, "' in preprocessor expression, in file ",
									 filestack_.empty() ? "<unknown>" : filestack_.top().file_name,
									" at line ", filestack_.empty() ? 0 : filestack_.top().line_number);
					values.push(0);
				}
				else {
					values.push(value);
					FLASH_LOG(Lexer, Trace, "  Pushed value: ", value, " (values.size=", values.size(), ")");
				}
			}
			else if (auto it = char_info_table.find(c); it != char_info_table.end()) {
				CharInfo info = it->second;
				op_str = iss.get(); // Consume the operator

				// Handle multi-character operators
				if (info.is_multi_char && (iss.peek() == '=' || (c != '!' && iss.peek() == c))) {
					op_str += iss.get();
				}

				const Operator op = string_to_operator(op_str);
				
				if (settings_.isVerboseMode()) {
					FLASH_LOG(Lexer, Trace, "  Found operator: '", op_str, "' (values.size=", values.size(), ", ops.size=", ops.size(), ")");
				}

				if (c == '(') {
					ops.push(op);
				}
				else if (c == ')') {
					while (!ops.empty() && ops.top() != Operator::OpenParen) {
						apply_operator(values, ops);
					}
					if (!ops.empty() && ops.top() == Operator::OpenParen) {
						ops.pop(); // Remove the '(' from the stack
					}
				}
				else {
					while (!ops.empty() && op != Operator::Not && precedence_table[op] <= precedence_table[ops.top()]) {
						apply_operator(values, ops);
					}
					ops.push(op);
				}
			}
			else if (isalpha(c) || c == '_') {
				// Manually consume only identifier characters to avoid consuming operators
				std::string keyword;
				while (iss) {
					char next = iss.peek();
					if (!isalnum(next) && next != '_') break;
					keyword += iss.get();
				}
				if (keyword == "defined") {
					std::string symbol;
					bool has_parenthesis = false;
					if (iss.peek() == '(') {
						iss.ignore(); // Consume the '('
						has_parenthesis = true;
					}
					iss >> symbol;
					if (has_parenthesis) {
						// The symbol may have ')' at the end if it was read by >> operator
						// Remove ')' from the symbol, but don't call ignore() because >> already consumed it
						symbol.erase(std::remove(symbol.begin(), symbol.end(), ')'), symbol.end());
					}

					const bool value = defines_.count(symbol) > 0;
					values.push(value);
					if (settings_.isVerboseMode()) {
						FLASH_LOG(Lexer, Trace, "  Pushed defined() result: ", value, " (symbol='", symbol, "', values.size=", values.size(), ")");
						// Don't print stream state here anymore since it was misleading
					}
				}
				else if (keyword == "__has_include") {
					// __has_include(<header>) or __has_include("header") - check if header exists
					// Read the argument from the input stream
					long exists = 0;
					char include_name_buf[256] = {};
					
					// Skip whitespace and expect '('
					iss >> std::ws;
					if (iss.peek() == '(') {
						iss.ignore(); // Consume '('
						
						// Skip whitespace after '('
						iss >> std::ws;
						
						// Check for < or "
						char quote_char = iss.peek();
						char end_char = (quote_char == '<') ? '>' : '"';
						
						if (quote_char == '<' || quote_char == '"') {
							iss.ignore(); // Consume opening < or "
							
							// Read the include name into buffer
							size_t i = 0;
							while (i < sizeof(include_name_buf) - 1 && iss && iss.peek() != end_char) {
								include_name_buf[i++] = iss.get();
							}
							include_name_buf[i] = '\0';
							
							// Consume closing > or "
							if (iss.peek() == end_char) {
								iss.ignore();
							}
							
							// Skip whitespace before ')'
							iss >> std::ws;
							
							// Consume closing ')' if present
							if (iss.peek() == ')') {
								iss.ignore();
							}
							
							std::string_view include_name(include_name_buf);
							
							// Check if the file exists in any include directory
							for (const auto& include_dir : settings_.getIncludeDirs()) {
								std::string include_file(include_dir);
								include_file.append("/");
								include_file.append(include_name);
								if (std::filesystem::exists(include_file)) {
									exists = 1;
									break;
								}
							}
							
							if (settings_.isVerboseMode()) {
								std::cout << "__has_include(" << quote_char << include_name << end_char << ") = " << exists << std::endl;
							}
						}
					}
					values.push(exists);
				}
				else if (keyword == "__has_builtin") {
					// __has_builtin(__builtin_name) - check if a compiler builtin is supported
					// Read the argument from the input stream
					long exists = 0;
					char builtin_name_buf[128] = {};
					
					// Skip whitespace and expect '('
					iss >> std::ws;
					if (iss.peek() == '(') {
						iss.ignore(); // Consume '('
						
						// Skip whitespace after '(' (allows "__has_builtin( __is_void)")
						iss >> std::ws;
						
						// Read the builtin name into buffer
						size_t i = 0;
						while (i < sizeof(builtin_name_buf) - 1 && iss && iss.peek() != ')' && !std::isspace(iss.peek())) {
							builtin_name_buf[i++] = iss.get();
						}
						builtin_name_buf[i] = '\0';
						
						// Skip whitespace before ')' (allows "__has_builtin(__is_void )")
						iss >> std::ws;
						
						// Consume closing ')' if present
						if (iss.peek() == ')') {
							iss.ignore();
						}
						
						std::string_view builtin_name(builtin_name_buf);
						
						// Set of all supported type trait and other compiler builtins
						// This must match the builtins supported in Parser.cpp
						static const std::unordered_set<std::string_view> supported_builtins = {
							// Type category traits
							"__is_void", "__is_nullptr", "__is_integral", "__is_floating_point",
							"__is_array", "__is_pointer", "__is_lvalue_reference", "__is_rvalue_reference",
							"__is_member_object_pointer", "__is_member_function_pointer",
							"__is_enum", "__is_union", "__is_class", "__is_function",
							// Composite type category traits
							"__is_reference", "__is_arithmetic", "__is_fundamental",
							"__is_object", "__is_scalar", "__is_compound",
							// Type relationship traits
							"__is_base_of", "__is_same", "__is_convertible", "__is_nothrow_convertible",
							// Type property traits
							"__is_polymorphic", "__is_final", "__is_abstract", "__is_empty",
							"__is_aggregate", "__is_standard_layout",
							"__has_unique_object_representations",
							"__is_trivially_copyable", "__is_trivial", "__is_pod",
							"__is_const", "__is_volatile", "__is_signed", "__is_unsigned",
							"__is_bounded_array", "__is_unbounded_array",
							// Type construction/destruction traits
							"__is_constructible", "__is_trivially_constructible", "__is_nothrow_constructible",
							"__is_assignable", "__is_trivially_assignable", "__is_nothrow_assignable",
							"__is_destructible", "__is_trivially_destructible", "__is_nothrow_destructible",
							"__has_trivial_destructor",  // GCC/Clang intrinsic, equivalent to __is_trivially_destructible
							// Layout traits
							"__is_layout_compatible", "__is_pointer_interconvertible_base_of",
							// Constant evaluation
							"__is_constant_evaluated",
							// Virtual destructor check
							"__has_virtual_destructor",
							// Builtin functions
							"__builtin_addressof", "__builtin_unreachable", "__builtin_assume",
							"__builtin_expect", "__builtin_launder",
							// Type modification - NOT YET IMPLEMENTED, using template fallbacks
							// "__remove_cv", "__remove_cvref", "__remove_reference",
							// "__add_lvalue_reference", "__add_rvalue_reference",
							// "__add_pointer", "__decay",
							// "__make_signed", "__make_unsigned",
							// Type inspection
							"__underlying_type",
							// Pack and tuple support
							"__type_pack_element"
						};
						
						exists = supported_builtins.count(builtin_name) > 0 ? 1 : 0;
						
						if (settings_.isVerboseMode()) {
							std::cout << "__has_builtin(" << builtin_name << ") = " << exists << std::endl;
						}
					}
					values.push(exists);
				}
				else if (keyword == "__has_cpp_attribute") {
					// __has_cpp_attribute(attribute_name) - check C++ attribute support
					// Read the argument from the input stream
					long version = 0;
					char attribute_name_buf[128] = {};
					
					// Skip whitespace and expect '('
					iss >> std::ws;
					if (iss.peek() == '(') {
						iss.ignore(); // Consume '('
						
						// Skip whitespace after '('
						iss >> std::ws;
						
						// Read the attribute name into buffer
						size_t i = 0;
						while (i < sizeof(attribute_name_buf) - 1 && iss && iss.peek() != ')' && !std::isspace(iss.peek())) {
							attribute_name_buf[i++] = iss.get();
						}
						attribute_name_buf[i] = '\0';
						
						// Skip whitespace before ')'
						iss >> std::ws;
						
						// Consume closing ')' if present
						if (iss.peek() == ')') {
							iss.ignore();
						}
						
						std::string_view attribute_name(attribute_name_buf);
						
						// Check if the attribute is supported and get its version
						if (auto attr_it = has_cpp_attribute_versions.find(attribute_name); attr_it != has_cpp_attribute_versions.end()) {
							version = attr_it->second;
						}
						
						if (settings_.isVerboseMode()) {
							std::cout << "__has_cpp_attribute(" << attribute_name << ") = " << version << std::endl;
						}
					}
					values.push(version);
				}
				else if (auto define_it = defines_.find(keyword); define_it != defines_.end()) {
					// convert the value to an int
					const auto& body = define_it->second.getBody();
					if (!body.empty()) {
						long value = 0;
						std::istringstream body_iss(body);
						std::string literal;
						if (!parseIntegerLiteral(body_iss, value, &literal)) {
							FLASH_LOG_FORMAT(Lexer, Warning, "Non-integer macro value in #if directive: ", keyword, "='", body, "' literal='", literal, "' at ",
								filestack_.top().file_name, ":", filestack_.top().line_number);
							values.push(0);
						}
						else {
							values.push(value);
						}
					}
					else {
						if (settings_.isVerboseMode()) {
							std::cout << "Checking unknown keyword value in #if directive: " << keyword << std::endl;
						}
						values.push(0);
					}
				}
				else {
					if (settings_.isVerboseMode()) {
						std::cout << "Checking unknown keyword in #if directive: " << keyword << std::endl;
					}
					values.push(0);
				}
			}
			else {
				c = iss.get();
			}
		}

		while (!ops.empty()) {
			apply_operator(values, ops);
		}

		if (eval_loop_guard == 0) {
			FLASH_LOG(Lexer, Error, "Expression evaluation loop limit reached (possible infinite loop in #if)");
			return 0;
		}

		if (values.size() == 0) {
			FLASH_LOG(Lexer, Error, "Internal compiler error, mismatched operator in file ", filestack_.top().file_name, ":", filestack_.top().line_number,
			          settings_.isVerboseMode() ? (std::string(" - values stack is empty, ops.size()=") + std::to_string(ops.size())) : "");
			return 0;
		}

		if (settings_.isVerboseMode()) {
			FLASH_LOG(Lexer, Trace, "Expression result: ", values.top(), " (values.size=", values.size(), ", ops.size=", ops.size(), ")");
		}

		return values.top();
	}

	bool processIncludeDirective(const std::string& line, const std::string_view& current_file, long include_line_number) {
		std::istringstream iss(line);
		std::string token;
		iss >> token;
		if (iss.eof() || token != "#include") {
			return true;
		}
		iss >> token;
		if (token.size() < 2 || (token.front() != '"' && token.front() != '<') || (token.front() == '"' && token.back() != '"') || (token.front() == '<' && token.back() != '>')) {
			return true;
		}
		std::string filename(token.substr(1, token.size() - 2));
		bool is_quoted_include = (token.front() == '"');
		bool found = false;
		if (settings_.isVerboseMode()) {
			FLASH_LOG(Lexer, Trace, "Looking for include file: ", filename);
		}
		// For quoted includes (#include "file.h"), first search the directory of the current file
		// This is standard C/C++ behavior per [cpp.include]
		if (is_quoted_include && !current_file.empty()) {
			std::filesystem::path current_dir = std::filesystem::path(current_file).parent_path();
			std::filesystem::path include_path = current_dir / filename;
			std::string include_file = include_path.string();
			if (settings_.isVerboseMode()) {
				FLASH_LOG(Lexer, Trace, "  Checking current file dir: ", include_file, " - exists: ", std::filesystem::exists(include_file));
			}
			if (std::filesystem::exists(include_file)) {
				if (settings_.isVerboseMode()) {
					FLASH_LOG(Lexer, Trace, "Found include file in current dir, attempting to read: ", include_file);
				}
				if (!readFile(include_file, include_line_number)) {
					if (settings_.isVerboseMode()) {
						FLASH_LOG(Lexer, Trace, "readFile returned false for: ", include_file);
					}
					return false;
				}
				tree_.addDependency(current_file, include_file);
				found = true;
			}
		}
		if (!found) {
			for (const auto& include_dir : settings_.getIncludeDirs()) {
				std::filesystem::path include_path(include_dir);
				include_path /= filename;  // Use /= operator which handles path separators correctly
				std::string include_file = include_path.string();
				if (settings_.isVerboseMode()) {
					FLASH_LOG(Lexer, Trace, "  Checking path: ", include_file, " - exists: ", std::filesystem::exists(include_file));
				}
				// Check if the file exists before trying to read it
				// This distinguishes between "file not found" and "file found but had preprocessing error"
				if (std::filesystem::exists(include_file)) {
					// File exists, try to read and preprocess it
					if (settings_.isVerboseMode()) {
						FLASH_LOG(Lexer, Trace, "Found include file, attempting to read: ", include_file);
					}
					if (!readFile(include_file, include_line_number)) {
						// Preprocessing failed (e.g., #error directive)
						// Return false to propagate the error up
						if (settings_.isVerboseMode()) {
							FLASH_LOG(Lexer, Trace, "readFile returned false for: ", include_file);
						}
						return false;
					}
					tree_.addDependency(current_file, include_file);
					found = true;
					break;
				}
			}
		}
		if (!found) {
			FLASH_LOG(Lexer, Error, "Failed to include file: ", filename);
			return false;
		}
		return true;
	}

	bool processIncludeNextDirective(const std::string& line, const std::string_view& current_file, long include_line_number) {
		// #include_next <header> - GCC extension
		// Searches for the header starting from the directory AFTER the one
		// where the current file was found. Used by C++ standard library headers
		// to include the underlying C headers (e.g., cmath -> math.h).
		std::istringstream iss(line);
		std::string token;
		iss >> token;
		if (iss.eof() || token != "#include_next") {
			return true;
		}
		iss >> token;
		if (token.size() < 2 || (token.front() != '"' && token.front() != '<') || (token.front() == '"' && token.back() != '"') || (token.front() == '<' && token.back() != '>')) {
			return true;
		}
		std::string filename(token.substr(1, token.size() - 2));
		if (settings_.isVerboseMode()) {
			FLASH_LOG(Lexer, Trace, "Looking for include_next file: ", filename, " (current: ", current_file, ")");
		}

		// Find which include directory contains the current file
		std::filesystem::path current_dir;
		if (!current_file.empty()) {
			current_dir = std::filesystem::path(std::string(current_file)).parent_path();
		}

		// Normalize current_dir for comparison
		std::string current_dir_str;
		if (!current_dir.empty()) {
			current_dir_str = std::filesystem::weakly_canonical(current_dir).string();
		}

		// Search include paths, skipping directories up to and including the one containing current_file
		bool found_current_dir = false;
		bool found = false;
		for (const auto& include_dir : settings_.getIncludeDirs()) {
			std::string canonical_include_dir = std::filesystem::weakly_canonical(std::filesystem::path(include_dir)).string();

			// Check if the current file is in this include directory (or a subdirectory of it)
			if (!found_current_dir && !current_dir_str.empty() &&
			    current_dir_str.find(canonical_include_dir) == 0 &&
			    (current_dir_str.size() == canonical_include_dir.size() ||
			     current_dir_str[canonical_include_dir.size()] == '/' ||
			     current_dir_str[canonical_include_dir.size()] == '\\')) {
				found_current_dir = true;
				if (settings_.isVerboseMode()) {
					FLASH_LOG(Lexer, Trace, "  Skipping include dir (contains current file): ", include_dir);
				}
				continue;  // Skip this directory
			}

			if (!found_current_dir) {
				continue;  // Haven't found the current dir yet, keep skipping
			}

			// Search in this directory
			std::filesystem::path include_path(include_dir);
			include_path /= filename;
			std::string include_file = include_path.string();
			if (settings_.isVerboseMode()) {
				FLASH_LOG(Lexer, Trace, "  include_next checking: ", include_file);
			}
			if (std::filesystem::exists(include_file)) {
				if (settings_.isVerboseMode()) {
					FLASH_LOG(Lexer, Trace, "Found include_next file: ", include_file);
				}
				if (!readFile(include_file, include_line_number)) {
					return false;
				}
				tree_.addDependency(current_file, include_file);
				found = true;
				break;
			}
		}

		if (!found && !found_current_dir) {
			// Fallback: if we couldn't find the current dir in include paths,
			// just do a regular include search (better than failing)
			if (settings_.isVerboseMode()) {
				FLASH_LOG(Lexer, Trace, "include_next fallback to regular include for: ", filename);
			}
			return processIncludeDirective("#include <" + filename + ">", current_file, include_line_number);
		} else if (!found) {
			FLASH_LOG(Lexer, Error, "#include_next: file not found after current directory: ", filename);
			return false;
		}
		return true;
	}

	void processPragmaPack(std::string_view line) {
		// Parse #pragma pack directives
		// Supported formats:
		//   #pragma pack()           - reset to default (no packing)
		//   #pragma pack(n)          - set pack alignment to n (1, 2, 4, 8, 16)
		//   #pragma pack(push)       - push current alignment onto stack
		//   #pragma pack(push, n)    - push current alignment and set to n
		//   #pragma pack(pop)        - pop alignment from stack

		// Find the opening parenthesis
		size_t open_paren = line.find('(');
		if (open_paren == std::string::npos) {
			// No parenthesis - ignore (malformed pragma)
			return;
		}

		size_t close_paren = line.find(')', open_paren);
		if (close_paren == std::string::npos) {
			// No closing parenthesis - ignore (malformed pragma)
			return;
		}

		// Extract content between parentheses
		std::string_view content = line.substr(open_paren + 1, close_paren - open_paren - 1);

		// Trim whitespace
		auto trim_start = content.find_first_not_of(" \t"sv);
		auto trim_end = content.find_last_not_of(" \t"sv);
		if (trim_start != std::string_view::npos && trim_end != std::string_view::npos) {
			content = content.substr(trim_start, trim_end - trim_start + 1);
		} else {
			content = {};
		}

		// Handle empty parentheses: #pragma pack()
		if (content.empty()) {
			settings_.setPackAlignment(0);  // Reset to default (no packing)
			return;
		}

		// Check for push/pop
		if (content == "push"sv) {
			settings_.pushPackAlignment();
			return;
		}

		if (content == "pop"sv) {
			settings_.popPackAlignment();
			return;
		}

		// Check for "push, n" format
		size_t comma_pos = content.find(',');
		if (comma_pos != std::string_view::npos) {
			std::string_view first_part = content.substr(0, comma_pos);
			std::string_view second_part = content.substr(comma_pos + 1);

			// Trim both parts
			auto trim_first_start = first_part.find_first_not_of(" \t");
			auto trim_first_end = first_part.find_last_not_of(" \t");
			if (trim_first_start != std::string_view::npos && trim_first_end != std::string_view::npos) {
				first_part = first_part.substr(trim_first_start, trim_first_end - trim_first_start + 1);
			}

			auto trim_second_start = second_part.find_first_not_of(" \t");
			auto trim_second_end = second_part.find_last_not_of(" \t");
			if (trim_second_start != std::string_view::npos && trim_second_end != std::string_view::npos) {
				second_part = second_part.substr(trim_second_start, trim_second_end - trim_second_start + 1);
			}

			if (first_part == "push") {
				// Parse the alignment value
				try {
					size_t alignment = 0;
					std::from_chars(second_part.data(), second_part.data() + second_part.size(), alignment);
					// Validate alignment (must be 0, 1, 2, 4, 8, or 16)
					if (alignment == 0 || alignment == 1 || alignment == 2 ||
					    alignment == 4 || alignment == 8 || alignment == 16) {
						settings_.pushPackAlignment(alignment);
					}
					// Invalid alignment values are silently ignored (matches MSVC behavior)
				} catch (...) {
					// Invalid number - ignore
				}
			}
			return;
		}

		// Otherwise, try to parse as a single number: #pragma pack(n)
		try {
			size_t alignment = 0;
			std::from_chars(content.data(), content.data() + content.size(), alignment);
			// Validate alignment (must be 0, 1, 2, 4, 8, or 16)
			if (alignment == 0 || alignment == 1 || alignment == 2 ||
			    alignment == 4 || alignment == 8 || alignment == 16) {
				settings_.setPackAlignment(alignment);
			}
			// Invalid alignment values are silently ignored (matches MSVC behavior)
		} catch (...) {
			// Invalid number - ignore
		}
	}

	void processLineDirective(const std::string& line) {
		// #line directive format:
		// #line line_number
		// #line line_number "filename"
		std::istringstream iss(line);
		iss.seekg("#line"sv.length());

		long new_line_number;
		iss >> new_line_number;

		if (iss.fail()) {
			FLASH_LOG(Lexer, Error, "Invalid #line directive: expected line number");
			return;
		}

		// Update the current line number (will be incremented on next line)
		if (!filestack_.empty()) {
			filestack_.top().line_number = new_line_number - 1;
		}

		// Check if there's a filename
		std::string filename;
		iss >> std::ws;  // Skip whitespace
		if (!iss.eof()) {
			std::getline(iss, filename);
			// Remove quotes if present
			if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"') {
				filename = filename.substr(1, filename.size() - 2);
			}
			// Update the filename
			if (!filestack_.empty()) {
				// We need to store the filename somewhere persistent
				// For now, we'll just update the file_name in the stack
				// Note: This is a bit tricky because file_name is a string_view
				// In a real implementation, we'd need to manage the lifetime properly
				// For now, we'll skip updating the filename to avoid lifetime issues
				// TODO: Properly handle filename updates in #line directives
			}
		}
	}

	void handleDefine(std::istringstream& iss) {
		DefineDirective define;

		// Parse the name
		std::string name;
		iss >> name;

		// Check for the presence of a macro argument list
		// A function-like macro has '(' immediately after the name (no space).
		// When parsed with >>, the name will include the '(' if there's no space.
		// An object-like macro may have '(' in its body, but there's a space before it.
		std::string rest_of_line;
		std::getline(iss >> std::ws, rest_of_line);
		size_t open_paren = name.find("(");
		bool is_function_like = (open_paren != std::string::npos);

		if (is_function_like) {
			// Function-like macro: prepend the '(' and everything after it from name to rest_of_line
			// E.g., name="FOO(x)" -> name="FOO", rest_of_line="(x) body"
			rest_of_line.insert(0, name.substr(open_paren));
			name.erase(open_paren);
		}

		if (!rest_of_line.empty()) {
			open_paren = rest_of_line.find("(");
			// Only parse as function-like macro arguments if:
			// 1. We detected this is a function-like macro (name originally had '(')
			// 2. rest_of_line starts with '('
			if (is_function_like && open_paren != std::string::npos && rest_of_line.find_first_not_of(' ') == open_paren) {
				size_t close_paren = rest_of_line.find(")", open_paren);

				if (close_paren == std::string::npos) {
					FLASH_LOG(Lexer, Error, "Missing closing parenthesis in macro argument list for ", name);
					return;
				}

				std::string arg_list = rest_of_line.substr(open_paren + 1, close_paren - open_paren - 1);

				// Tokenize the argument list
				std::istringstream arg_stream(arg_list);
				std::string token;
				bool found_variadic_args = false;
				while (std::getline(arg_stream, token, ',')) {
					// Remove leading and trailing whitespace
					auto start = std::find_if_not(token.begin(), token.end(), [](unsigned char c) { return std::isspace(c); });
					auto end = std::find_if_not(token.rbegin(), token.rend(), [](unsigned char c) { return std::isspace(c); }).base();
					token = std::string(start, end);

					if (token == "..." && !found_variadic_args) {
						found_variadic_args = true;
					}
					else if (token == "..." && found_variadic_args) {
						FLASH_LOG(Lexer, Error, "Duplicate variadic arguments '...' detected in macro argument list for ", name);
						return;
					}
					else {
						define.args.push_back(std::move(token));
						token = std::string();	// it's undefined behavior to move a string and then use it again
					}
				}

				// Save the macro body after the closing parenthesis
				rest_of_line.erase(0, rest_of_line.find_first_not_of(' ', close_paren + 1));
				define.is_function_like = true;  // This is a function-like macro
			}
			else {
				rest_of_line.erase(0, rest_of_line.find_first_not_of(' '));
			}
		}

		define.body = std::move(rest_of_line);

		// Add the parsed define to the map
		defines_[name] = std::move(define);
	}

	void addBuiltinDefines() {
		// Add __cplusplus with the value corresponding to the C++ standard in use
		defines_["__cplusplus"] = DefineDirective{ "202002L", {} };  // C++20
		defines_["__STDC_HOSTED__"] = DefineDirective{ "1", {} };
		defines_["__STDCPP_THREADS__"] = DefineDirective{ "1", {} };
		defines_["_LIBCPP_LITTLE_ENDIAN"] = DefineDirective{};
		
		// GCC compatibility macros (needed for standard library headers like wchar.h)
		// These allow __GNUC_PREREQ checks to pass and expose C++ overloads
		defines_["__GNUC__"] = DefineDirective{ "12", {} };  // GCC 12.x compatible
		defines_["__GNUC_MINOR__"] = DefineDirective{ "2", {} };
		defines_["__GNUC_PATCHLEVEL__"] = DefineDirective{ "0", {} };
		defines_["__GNUG__"] = DefineDirective{ "12", {} };  // C++ compiler version
		defines_["__restrict"] = DefineDirective{};  // Strip __restrict keyword (not supported yet)
		defines_["__extension__"] = DefineDirective{};  // Strip __extension__ keyword (GCC extension)
		
		// GCC atomic memory ordering macros (used by <atomic>, <iostream> via atomicity.h)
		defines_["__ATOMIC_RELAXED"] = DefineDirective{ "0", {} };
		defines_["__ATOMIC_CONSUME"] = DefineDirective{ "1", {} };
		defines_["__ATOMIC_ACQUIRE"] = DefineDirective{ "2", {} };
		defines_["__ATOMIC_RELEASE"] = DefineDirective{ "3", {} };
		defines_["__ATOMIC_ACQ_REL"] = DefineDirective{ "4", {} };
		defines_["__ATOMIC_SEQ_CST"] = DefineDirective{ "5", {} };
		
		// GCC libstdc++ macros
		defines_["_GLIBCXX_VISIBILITY"] = DefineDirective{ "", { "V" }, true };
		defines_["_GLIBCXX_BEGIN_NAMESPACE_VERSION"] = DefineDirective{};  // Inline namespace for versioning
		defines_["_GLIBCXX_END_NAMESPACE_VERSION"] = DefineDirective{};  // Inline namespace for versioning
		defines_["_GLIBCXX_DEPRECATED"] = DefineDirective{};  // Strip deprecated attributes
		defines_["_GLIBCXX_DEPRECATED_SUGGEST"] = DefineDirective{ "", { "ALT" }, true };
		defines_["_GLIBCXX11_DEPRECATED"] = DefineDirective{};  // Strip C++11 deprecated attributes
		defines_["_GLIBCXX11_DEPRECATED_SUGGEST"] = DefineDirective{ "", { "ALT" }, true };
		defines_["_GLIBCXX14_DEPRECATED"] = DefineDirective{};  // Strip C++14 deprecated attributes
		defines_["_GLIBCXX14_DEPRECATED_SUGGEST"] = DefineDirective{ "", { "ALT" }, true };
		defines_["_GLIBCXX17_DEPRECATED"] = DefineDirective{};  // Strip C++17 deprecated attributes
		defines_["_GLIBCXX17_DEPRECATED_SUGGEST"] = DefineDirective{ "", { "ALT" }, true };
		defines_["_GLIBCXX20_DEPRECATED"] = DefineDirective{ "", { "MSG" }, true };
		defines_["_GLIBCXX23_DEPRECATED"] = DefineDirective{};  // Strip C++23 deprecated attributes
		defines_["_GLIBCXX23_DEPRECATED_SUGGEST"] = DefineDirective{ "", { "ALT" }, true };
		defines_["_GLIBCXX_NODISCARD"] = DefineDirective{};  // Strip nodiscard attributes
		defines_["_GLIBCXX_PURE"] = DefineDirective{};  // Strip pure attributes
		defines_["_GLIBCXX_CONST"] = DefineDirective{};  // Strip const attributes
		defines_["_GLIBCXX_NORETURN"] = DefineDirective{};  // Strip noreturn attributes
		defines_["_GLIBCXX_NOTHROW"] = DefineDirective{};  // Strip nothrow attributes
		defines_["_GLIBCXX_NOEXCEPT"] = DefineDirective{ "noexcept", {} };  // Map to noexcept keyword
		defines_["_GLIBCXX_USE_NOEXCEPT"] = DefineDirective{ "noexcept", {} };  // Map to noexcept keyword (C++11 mode)
		defines_["_GLIBCXX_NOEXCEPT_IF"] = DefineDirective{ "noexcept(_Cond)", { "_Cond" }, true };
		defines_["_GLIBCXX_NOEXCEPT_QUAL"] = DefineDirective{};  // Strip noexcept qualifier (works with _GLIBCXX_NOEXCEPT_PARM)
		defines_["_GLIBCXX_NOEXCEPT_PARM"] = DefineDirective{};  // Strip noexcept parameter specifier (works with _GLIBCXX_NOEXCEPT_QUAL)
		defines_["_GLIBCXX_THROW"] = DefineDirective{ "", { "_Spec" }, true };  // Strip old-style exception specifications throw(...)
		defines_["_GLIBCXX_THROW_OR_ABORT"] = DefineDirective{};  // Strip exception specs
		defines_["_GLIBCXX_TXN_SAFE"] = DefineDirective{};  // Strip transactional memory attributes
		defines_["_GLIBCXX_TXN_SAFE_DYN"] = DefineDirective{};  // Strip transactional memory attributes
		// GCC assembler symbol renaming directive - strip it
		// Used in glibc headers like: extern wchar_t *wcschr() __asm ("wcschr");
		// Also strip __asm__ variant (with two underscores on each side)
		defines_["__asm"] = DefineDirective{ "", { "x" }, true };  // Strip __asm("symbol_name") directives
		defines_["__asm__"] = DefineDirective{ "", { "x" }, true };  // Strip __asm__("symbol_name") directives
		defines_["_GLIBCXX_USE_CXX11_ABI"] = DefineDirective{ "1", {} };  // Use C++11 ABI for std::string and std::list
		// C++11 ABI namespace macros (when _GLIBCXX_USE_CXX11_ABI is 1)
		defines_["_GLIBCXX_NAMESPACE_CXX11"] = DefineDirective{ "__cxx11::", {} };
		defines_["_GLIBCXX_BEGIN_NAMESPACE_CXX11"] = DefineDirective{ "namespace __cxx11 {", {} };
		defines_["_GLIBCXX_END_NAMESPACE_CXX11"] = DefineDirective{ "}", {} };
		defines_["_GLIBCXX_NAMESPACE_LDBL_OR_CXX11"] = DefineDirective{ "__cxx11::", {} };
		defines_["_GLIBCXX_BEGIN_NAMESPACE_LDBL_OR_CXX11"] = DefineDirective{ "namespace __cxx11 {", {} };
		defines_["_GLIBCXX_END_NAMESPACE_LDBL_OR_CXX11"] = DefineDirective{ "}", {} };
		// Container namespace macros (for std::list iterators)
		defines_["_GLIBCXX_BEGIN_NAMESPACE_CONTAINER"] = DefineDirective{};  // Strip container namespace
		defines_["_GLIBCXX_END_NAMESPACE_CONTAINER"] = DefineDirective{};  // Strip container namespace
		defines_["_GLIBCXX_CONSTEXPR"] = DefineDirective{ "constexpr", {} };  // Enable constexpr
		defines_["_GLIBCXX_USE_CONSTEXPR"] = DefineDirective{ "constexpr", {} };  // Enable constexpr
		defines_["_GLIBCXX14_CONSTEXPR"] = DefineDirective{ "constexpr", {} };  // C++14 constexpr
		defines_["_GLIBCXX17_CONSTEXPR"] = DefineDirective{ "constexpr", {} };  // C++17 constexpr
		defines_["_GLIBCXX17_INLINE"] = DefineDirective{ "inline", {} };  // C++17 inline variables
		defines_["_GLIBCXX20_CONSTEXPR"] = DefineDirective{ "constexpr", {} };  // C++20 constexpr
		defines_["_GLIBCXX23_CONSTEXPR"] = DefineDirective{ "constexpr", {} };  // C++23 constexpr
		defines_["_GLIBCXX_INLINE_VERSION"] = DefineDirective{ "0", {} };  // Inline namespace version (0 = no versioning)
		defines_["_GLIBCXX_ABI_TAG_CXX11"] = DefineDirective{};  // Strip ABI tags
		defines_["_GLIBCXX_USE_WCHAR_T"] = DefineDirective{ "1", {} };  // Enable wchar_t support and wide char functions
		
		// MSVC C++ standard version feature flags (cumulative)
		defines_["_HAS_CXX17"] = DefineDirective{ "1", {} };  // C++17 features available
		defines_["_HAS_CXX20"] = DefineDirective{ "1", {} };  // C++20 features available
		defines_["_MSVC_LANG"] = DefineDirective{ "202002L", {} };  // MSVC language version (C++20)

		// FlashCpp compiler identification
		defines_["__FLASHCPP__"] = DefineDirective{ "1", {} };
		defines_["__FLASHCPP_VERSION__"] = DefineDirective{ "1", {} };
		defines_["__FLASHCPP_VERSION_MAJOR__"] = DefineDirective{ "0", {} };
		defines_["__FLASHCPP_VERSION_MINOR__"] = DefineDirective{ "1", {} };
		defines_["__FLASHCPP_VERSION_PATCH__"] = DefineDirective{ "0", {} };

		// Windows platform macros
		defines_["_WIN32"] = DefineDirective{ "1", {} };
		defines_["_WIN64"] = DefineDirective{ "1", {} };
		defines_["_MSC_VER"] = DefineDirective{ "1944", {} };  // MSVC 2022 (match clang behavior)
		defines_["_MSC_FULL_VER"] = DefineDirective{ "194435217", {} };  // MSVC 2022 full version
		defines_["_MSC_BUILD"] = DefineDirective{ "1", {} };
		defines_["_MSC_EXTENSIONS"] = DefineDirective{ "1", {} };  // Enable MSVC extensions
		
		// MSVC STL macros
		defines_["_HAS_EXCEPTIONS"] = DefineDirective{ "1", {} };  // Exception handling enabled
		defines_["_CPPRTTI"] = DefineDirective{ "1", {} };  // RTTI enabled
		defines_["_NATIVE_WCHAR_T_DEFINED"] = DefineDirective{ "1", {} };  // wchar_t is native type
		defines_["_WCHAR_T_DEFINED"] = DefineDirective{ "1", {} };
		
		// Additional common MSVC macros
		defines_["_INTEGRAL_MAX_BITS"] = DefineDirective{ "64", {} };
		defines_["_MT"] = DefineDirective{ "1", {} };  // Multithreaded
		defines_["_DLL"] = DefineDirective{ "1", {} };  // Using DLL runtime

		// Architecture macros
		defines_["__x86_64__"] = DefineDirective{ "1", {} };
		defines_["__amd64__"] = DefineDirective{ "1", {} };
		defines_["__amd64"] = DefineDirective{ "1", {} };
		defines_["_M_X64"] = DefineDirective{ "100", {} };  // MSVC-style
		defines_["_M_AMD64"] = DefineDirective{ "100", {} };

		// Byte order macros (needed by <compare> and other headers)
		defines_["__ORDER_LITTLE_ENDIAN__"] = DefineDirective{ "1234", {} };
		defines_["__ORDER_BIG_ENDIAN__"] = DefineDirective{ "4321", {} };
		defines_["__ORDER_PDP_ENDIAN__"] = DefineDirective{ "3412", {} };
		defines_["__BYTE_ORDER__"] = DefineDirective{ "__ORDER_LITTLE_ENDIAN__", {} };  // x86_64 is little endian

		// C++ feature test macros (SD-6)
		// These indicate which C++ language features are supported
		defines_["__cpp_aggregate_bases"] = DefineDirective{ "201603L", {} };  // C++17 aggregate base classes
		defines_["__cpp_aggregate_nsdmi"] = DefineDirective{ "201304L", {} };  // Aggregate NSDMI
		defines_["__cpp_aggregate_paren_init"] = DefineDirective{ "201902L", {} };  // Aggregate direct init
		defines_["__cpp_alias_templates"] = DefineDirective{ "200704L", {} };  // Alias templates
		defines_["__cpp_aligned_new"] = DefineDirective{ "201606L", {} };  // Over-aligned new
		defines_["__cpp_attributes"] = DefineDirective{ "200809L", {} };  // Attributes
		defines_["__cpp_auto_type"] = DefineDirective{ "200606L", {} };  // auto type deduction
		defines_["__cpp_binary_literals"] = DefineDirective{ "201304L", {} };  // Binary literals
		defines_["__cpp_capture_star_this"] = DefineDirective{ "201603L", {} };  // Lambda capture *this by value
		defines_["__cpp_char8_t"] = DefineDirective{ "201811L", {} };  // char8_t
		defines_["__cpp_concepts"] = DefineDirective{ "201907L", {} };  // C++20 concepts
		defines_["__cpp_conditional_explicit"] = DefineDirective{ "201806L", {} };  // explicit(bool)
		defines_["__cpp_conditional_trivial"] = DefineDirective{ "202002L", {} };  // Conditional trivial special members
		defines_["__cpp_consteval"] = DefineDirective{ "201811L", {} };  // consteval
		defines_["__cpp_constexpr"] = DefineDirective{ "202002L", {} };  // C++20 constexpr extensions
		defines_["__cpp_constexpr_dynamic_alloc"] = DefineDirective{ "201907L", {} };  // constexpr dynamic alloc
		defines_["__cpp_constexpr_in_decltype"] = DefineDirective{ "201711L", {} };  // decltype during constant eval
		defines_["__cpp_constinit"] = DefineDirective{ "201907L", {} };  // constinit
		defines_["__cpp_decltype"] = DefineDirective{ "200707L", {} };  // decltype
		defines_["__cpp_decltype_auto"] = DefineDirective{ "201304L", {} };  // decltype(auto)
		defines_["__cpp_deduction_guides"] = DefineDirective{ "201907L", {} };  // CTAD for aggregates/aliases
		defines_["__cpp_delegating_constructors"] = DefineDirective{ "200604L", {} };  // Delegating constructors
		defines_["__cpp_designated_initializers"] = DefineDirective{ "201707L", {} };  // Designated initializers
		defines_["__cpp_enumerator_attributes"] = DefineDirective{ "201411L", {} };  // Enumerator attributes
		// __cpp_exceptions intentionally NOT defined - FlashCpp does not implement exception handling.
		// Standard library headers use #if __cpp_exceptions guards around try/catch code,
		// falling back to simpler non-exception alternatives when it's not defined.
		defines_["__cpp_fold_expressions"] = DefineDirective{ "201603L", {} };  // Fold expressions
		defines_["__cpp_generic_lambdas"] = DefineDirective{ "201707L", {} };  // Generic lambdas with template params
		defines_["__cpp_guaranteed_copy_elision"] = DefineDirective{ "201606L", {} };  // Guaranteed copy elision
		defines_["__cpp_hex_float"] = DefineDirective{ "201603L", {} };  // Hexadecimal float literals
		defines_["__cpp_if_constexpr"] = DefineDirective{ "201606L", {} };  // C++17 if constexpr
		defines_["__cpp_impl_coroutine"] = DefineDirective{ "201902L", {} };  // C++20 coroutine support
		defines_["__cpp_impl_destroying_delete"] = DefineDirective{ "201806L", {} };  // Destroying delete
		defines_["__cpp_impl_three_way_comparison"] = DefineDirective{ "201907L", {} };  // <=> support
		defines_["__cpp_inheriting_constructors"] = DefineDirective{ "200802L", {} };  // Inheriting constructors
		defines_["__cpp_init_captures"] = DefineDirective{ "201803L", {} };  // Lambda init-capture pack expansion
		defines_["__cpp_initializer_lists"] = DefineDirective{ "200806L", {} };  // Initializer lists
		defines_["__cpp_inline_variables"] = DefineDirective{ "201606L", {} };  // C++17 inline variables
		defines_["__cpp_lambdas"] = DefineDirective{ "200907L", {} };  // Lambda expressions
		//defines_["__cpp_modules"] = DefineDirective{ "201907L", {} };  // Modules
		defines_["__cpp_namespace_attributes"] = DefineDirective{ "201411L", {} };  // Namespace attributes
		defines_["__cpp_noexcept_function_type"] = DefineDirective{ "201510L", {} };  // C++17 noexcept in type
		defines_["__cpp_nontype_template_args"] = DefineDirective{ "201911L", {} };  // Class/float NTTP
		defines_["__cpp_nontype_template_parameter_auto"] = DefineDirective{ "201606L", {} };  // auto NTTP
		defines_["__cpp_nullptr"] = DefineDirective{ "200704L", {} };  // nullptr keyword
		defines_["__cpp_nsdmi"] = DefineDirective{ "200809L", {} };  // Non-static data member initializers
		defines_["__cpp_range_based_for"] = DefineDirective{ "201603L", {} };  // Range-based for (C++17 update)
		defines_["__cpp_raw_strings"] = DefineDirective{ "200710L", {} };  // Raw string literals
		defines_["__cpp_ref_qualifiers"] = DefineDirective{ "200710L", {} };  // Ref-qualified member funcs
		defines_["__cpp_return_type_deduction"] = DefineDirective{ "201304L", {} };  // Return type deduction
		defines_["__cpp_rtti"] = DefineDirective{ "199711L", {} };  // RTTI (typeid, dynamic_cast)
		defines_["__cpp_rvalue_references"] = DefineDirective{ "200610L", {} };  // Rvalue references
		defines_["__cpp_sized_deallocation"] = DefineDirective{ "201309L", {} };  // Sized deallocation
		defines_["__cpp_static_assert"] = DefineDirective{ "201411L", {} };  // C++17 static_assert with message
		defines_["__cpp_structured_bindings"] = DefineDirective{ "201606L", {} };  // C++17 structured bindings
		defines_["__cpp_template_template_args"] = DefineDirective{ "201611L", {} };  // Template template parameter matching
		defines_["__cpp_threadsafe_static_init"] = DefineDirective{ "200806L", {} };  // Thread-safe static init
		defines_["__cpp_unicode_characters"] = DefineDirective{ "200704L", {} };  // char16_t/char32_t
		defines_["__cpp_unicode_literals"] = DefineDirective{ "200710L", {} };  // Unicode string literals
		defines_["__cpp_user_defined_literals"] = DefineDirective{ "200809L", {} };  // User Defined Literals
		defines_["__cpp_using_enum"] = DefineDirective{ "201907L", {} };  // using enum (C++20)
		defines_["__cpp_variable_templates"] = DefineDirective{ "201304L", {} };  // Variable templates
		defines_["__cpp_variadic_templates"] = DefineDirective{ "200704L", {} };  // Variadic templates
		defines_["__cpp_variadic_using"] = DefineDirective{ "201611L", {} };  // Pack expansions in using

		// Note: __has_builtin is NOT defined as a macro here
		// It is handled specially in expandMacrosForConditional and evaluate_expression
		// to properly evaluate __has_builtin(builtin_name) at preprocessing time

		// C++ library feature test macros (SD-6)
		// These indicate which C++ standard library features are supported
		// Values are in format YYYYMML (year/month when feature was standardized)
		defines_["__cpp_lib_type_trait_variable_templates"] = DefineDirective{ "201510L", {} };  // C++17 (Oct 2015)
		defines_["__cpp_lib_addressof_constexpr"] = DefineDirective{ "201603L", {} };  // C++17 (Mar 2016)
		defines_["__cpp_lib_integral_constant_callable"] = DefineDirective{ "201304L", {} };  // C++14 (Apr 2013)
		defines_["__cpp_lib_is_aggregate"] = DefineDirective{ "201703L", {} };  // C++17 (Mar 2017)
		defines_["__cpp_lib_void_t"] = DefineDirective{ "201411L", {} };  // C++17 (Nov 2014)
		defines_["__cpp_lib_bool_constant"] = DefineDirective{ "201505L", {} };  // C++17 (May 2015)

		// Compiler builtin type macros - values depend on compiler mode
		// MSVC (default): Windows x64 types
		// GCC/Clang: Linux x64 types
		if (settings_.isMsvcMode()) {
			// MSVC x64 builtin types
			defines_["__SIZE_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__PTRDIFF_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__WCHAR_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__INTMAX_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINTMAX_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INTPTR_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINTPTR_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INT8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT64_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINT8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT64_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INT_LEAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_LEAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_LEAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_LEAST64_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINT_LEAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_LEAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_LEAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_LEAST64_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INT_FAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_FAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_FAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_FAST64_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINT_FAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_FAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_FAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_FAST64_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__SIG_ATOMIC_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__CHAR16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__CHAR32_TYPE__"] = DefineDirective{ "unsigned int", {} };
		} else if (settings_.isGccMode()) {
			// GCC/Clang x64 builtin types (Linux/macOS)
			defines_["__SIZE_TYPE__"] = DefineDirective{ "long unsigned int", {} };
			defines_["__PTRDIFF_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__WCHAR_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INTMAX_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINTMAX_TYPE__"] = DefineDirective{ "long unsigned int", {} };
			defines_["__INTPTR_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINTPTR_TYPE__"] = DefineDirective{ "long unsigned int", {} };
			defines_["__INT8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT64_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINT8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT64_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__INT_LEAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_LEAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_LEAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_LEAST64_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINT_LEAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_LEAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_LEAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_LEAST64_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__INT_FAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_FAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_FAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_FAST64_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINT_FAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_FAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_FAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_FAST64_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__SIG_ATOMIC_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__CHAR16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__CHAR32_TYPE__"] = DefineDirective{ "unsigned int", {} };

			// GCC/Clang specific predefined macros
			defines_["__STRICT_ANSI__"] = DefineDirective{ "1", {} };
			// _GNU_SOURCE enables POSIX/GNU features in glibc headers (e.g., uselocale in locale.h)
			// Both Clang and GCC define this by default on Linux, even with -std=c++20
			defines_["_GNU_SOURCE"] = DefineDirective{ "1", {} };
			if (settings_.getDataModel() == CompileContext::DataModel::LP64) {
				defines_["__ELF__"] = DefineDirective{ "1", {} };
			}
			defines_["__VERSION__"] = DefineDirective{ "\"FlashCpp (gcc compatibility)\"", {} };

			defines_["__BASE_FILE__"] = FunctionDirective{ [this]() -> std::string {
				// Prefer the main input file if available, otherwise fall back to the current file
				if (auto input = settings_.getInputFile()) {
					std::filesystem::path p(*input);
					return "\"" + p.generic_string() + "\"";
				}
				if (!filestack_.empty()) {
					std::filesystem::path p(filestack_.top().file_name);
					return "\"" + p.generic_string() + "\"";
				}
				return "\"\"";
			} };

			defines_["__FILE_NAME__"] = FunctionDirective{ [this]() -> std::string {
				if (filestack_.empty()) {
					return "\"\"";
				}
				std::filesystem::path file_path(filestack_.top().file_name);
				return "\"" + file_path.filename().generic_string() + "\"";
			} };

			// Integer limit macros
			defines_["__SIG_ATOMIC_MAX__"] = DefineDirective{ "2147483647", {} };
			defines_["__SIG_ATOMIC_MIN__"] = DefineDirective{ "(-2147483648)", {} };

			defines_["__INT_LEAST8_MAX__"] = DefineDirective{ "127", {} };
			defines_["__INT_LEAST16_MAX__"] = DefineDirective{ "32767", {} };
			defines_["__INT_LEAST32_MAX__"] = DefineDirective{ "2147483647", {} };
			defines_["__INT_LEAST64_MAX__"] = DefineDirective{ "9223372036854775807L", {} };
			defines_["__UINT_LEAST8_MAX__"] = DefineDirective{ "255", {} };
			defines_["__UINT_LEAST16_MAX__"] = DefineDirective{ "65535", {} };
			defines_["__UINT_LEAST32_MAX__"] = DefineDirective{ "4294967295U", {} };
			defines_["__UINT_LEAST64_MAX__"] = DefineDirective{ "18446744073709551615UL", {} };

			defines_["__INT_FAST8_MAX__"] = DefineDirective{ "127", {} };
			defines_["__INT_FAST16_MAX__"] = DefineDirective{ "32767", {} };
			defines_["__INT_FAST32_MAX__"] = DefineDirective{ "2147483647", {} };
			defines_["__INT_FAST64_MAX__"] = DefineDirective{ "9223372036854775807L", {} };
			defines_["__UINT_FAST8_MAX__"] = DefineDirective{ "255", {} };
			defines_["__UINT_FAST16_MAX__"] = DefineDirective{ "65535", {} };
			defines_["__UINT_FAST32_MAX__"] = DefineDirective{ "4294967295U", {} };
			defines_["__UINT_FAST64_MAX__"] = DefineDirective{ "18446744073709551615UL", {} };

			defines_["__INTPTR_MAX__"] = DefineDirective{ "9223372036854775807L", {} };
			defines_["__UINTPTR_MAX__"] = DefineDirective{ "18446744073709551615UL", {} };

			defines_["__WCHAR_MIN__"] = DefineDirective{ "(-2147483648)", {} };
			defines_["__WINT_MIN__"] = DefineDirective{ "0", {} };

			// Integer constant macros
			{
				DefineDirective macro{ "c", { "c" } };
				macro.is_function_like = true;
				defines_["__INT8_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c", { "c" } };
				macro.is_function_like = true;
				defines_["__INT16_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c", { "c" } };
				macro.is_function_like = true;
				defines_["__INT32_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c##L", { "c" } };
				macro.is_function_like = true;
				defines_["__INT64_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c", { "c" } };
				macro.is_function_like = true;
				defines_["__UINT8_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c##U", { "c" } };
				macro.is_function_like = true;
				defines_["__UINT16_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c##U", { "c" } };
				macro.is_function_like = true;
				defines_["__UINT32_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c##UL", { "c" } };
				macro.is_function_like = true;
				defines_["__UINT64_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c##L", { "c" } };
				macro.is_function_like = true;
				defines_["__INTMAX_C"] = std::move(macro);
			}
			{
				DefineDirective macro{ "c##UL", { "c" } };
				macro.is_function_like = true;
				defines_["__UINTMAX_C"] = std::move(macro);
			}
		}

		// Compiler builtin macros for numeric limits - required by <limits> header
		// These are common to both MSVC and GCC/Clang modes on x86_64
		defines_["__CHAR_BIT__"] = DefineDirective{ "8", {} };
		defines_["__SCHAR_MAX__"] = DefineDirective{ "127", {} };
		defines_["__SHRT_MAX__"] = DefineDirective{ "32767", {} };
		defines_["__INT_MAX__"] = DefineDirective{ "2147483647", {} };
		defines_["__LONG_LONG_MAX__"] = DefineDirective{ "9223372036854775807LL", {} };
		defines_["__WCHAR_MAX__"] = DefineDirective{ "2147483647", {} };
		defines_["__WINT_MAX__"] = DefineDirective{ "4294967295U", {} };
		
		// intmax_t and uintmax_t limits - required by <ratio> and <cstdint> headers
		// intmax_t is 64-bit on x64 platforms
		defines_["__INTMAX_MAX__"] = DefineDirective{ "9223372036854775807LL", {} };
		defines_["__INTMAX_MIN__"] = DefineDirective{ "(-9223372036854775807LL - 1)", {} };
		defines_["__UINTMAX_MAX__"] = DefineDirective{ "18446744073709551615ULL", {} };

		// Platform-specific __LONG_MAX__ (differs between Windows and Linux x64)
		if (settings_.isMsvcMode()) {
			defines_["__LONG_MAX__"] = DefineDirective{ "2147483647L", {} };  // 32-bit long on Windows
		} else {
			defines_["__LONG_MAX__"] = DefineDirective{ "9223372036854775807L", {} };  // 64-bit long on Linux
		}

		// Compiler builtin macros for sizeof types - required by <limits> header
		defines_["__SIZEOF_SHORT__"] = DefineDirective{ "2", {} };
		defines_["__SIZEOF_INT__"] = DefineDirective{ "4", {} };
		defines_["__SIZEOF_LONG_LONG__"] = DefineDirective{ "8", {} };
		defines_["__SIZEOF_FLOAT__"] = DefineDirective{ "4", {} };
		defines_["__SIZEOF_DOUBLE__"] = DefineDirective{ "8", {} };
		defines_["__SIZEOF_POINTER__"] = DefineDirective{ "8", {} };
		defines_["__SIZEOF_SIZE_T__"] = DefineDirective{ "8", {} };
		defines_["__SIZEOF_PTRDIFF_T__"] = DefineDirective{ "8", {} };
		defines_["__SIZEOF_WCHAR_T__"] = DefineDirective{ "4", {} };
		defines_["__SIZEOF_WINT_T__"] = DefineDirective{ "4", {} };

		// Platform-specific __SIZEOF_LONG__ (differs between Windows and Linux x64)
		if (settings_.isMsvcMode()) {
			defines_["__SIZEOF_LONG__"] = DefineDirective{ "4", {} };  // 32-bit long on Windows
		} else {
			defines_["__SIZEOF_LONG__"] = DefineDirective{ "8", {} };  // 64-bit long on Linux
		}

		// Floating-point limit macros - required by <limits> and <cfloat> headers
		// These values are for IEEE 754 floating-point (x86_64 architecture)
		// Float (32-bit IEEE 754)
		defines_["__FLT_RADIX__"] = DefineDirective{ "2", {} };
		defines_["__FLT_MANT_DIG__"] = DefineDirective{ "24", {} };
		defines_["__FLT_DIG__"] = DefineDirective{ "6", {} };
		defines_["__FLT_DECIMAL_DIG__"] = DefineDirective{ "9", {} };
		defines_["__FLT_MIN_EXP__"] = DefineDirective{ "(-125)", {} };
		defines_["__FLT_MIN_10_EXP__"] = DefineDirective{ "(-37)", {} };
		defines_["__FLT_MAX_EXP__"] = DefineDirective{ "128", {} };
		defines_["__FLT_MAX_10_EXP__"] = DefineDirective{ "38", {} };
		defines_["__FLT_MIN__"] = DefineDirective{ "1.17549435082228750796873653722224568e-38F", {} };
		defines_["__FLT_MAX__"] = DefineDirective{ "3.40282346638528859811704183484516925e+38F", {} };
		defines_["__FLT_EPSILON__"] = DefineDirective{ "1.19209289550781250000000000000000000e-7F", {} };
		defines_["__FLT_DENORM_MIN__"] = DefineDirective{ "1.40129846432481707092372958328991613e-45F", {} };
		defines_["__FLT_NORM_MAX__"] = DefineDirective{ "3.40282346638528859811704183484516925e+38F", {} };
		defines_["__FLT_HAS_DENORM__"] = DefineDirective{ "1", {} };
		defines_["__FLT_HAS_INFINITY__"] = DefineDirective{ "1", {} };
		defines_["__FLT_HAS_QUIET_NAN__"] = DefineDirective{ "1", {} };
		defines_["__FLT_IS_IEC_60559__"] = DefineDirective{ "1", {} };
		defines_["__FLT_EVAL_METHOD__"] = DefineDirective{ "0", {} };
		defines_["__FLT_EVAL_METHOD_TS_18661_3__"] = DefineDirective{ "0", {} };

		// Double (64-bit IEEE 754)
		defines_["__DBL_MANT_DIG__"] = DefineDirective{ "53", {} };
		defines_["__DBL_DIG__"] = DefineDirective{ "15", {} };
		defines_["__DBL_DECIMAL_DIG__"] = DefineDirective{ "17", {} };
		defines_["__DBL_MIN_EXP__"] = DefineDirective{ "(-1021)", {} };
		defines_["__DBL_MIN_10_EXP__"] = DefineDirective{ "(-307)", {} };
		defines_["__DBL_MAX_EXP__"] = DefineDirective{ "1024", {} };
		defines_["__DBL_MAX_10_EXP__"] = DefineDirective{ "308", {} };
		defines_["__DBL_MIN__"] = DefineDirective{ "((double)2.22507385850720138309023271733240406e-308L)", {} };
		defines_["__DBL_MAX__"] = DefineDirective{ "((double)1.79769313486231570814527423731704357e+308L)", {} };
		defines_["__DBL_EPSILON__"] = DefineDirective{ "((double)2.22044604925031308084726333618164062e-16L)", {} };
		defines_["__DBL_DENORM_MIN__"] = DefineDirective{ "((double)4.94065645841246544176568792868221372e-324L)", {} };
		defines_["__DBL_NORM_MAX__"] = DefineDirective{ "((double)1.79769313486231570814527423731704357e+308L)", {} };
		defines_["__DBL_HAS_DENORM__"] = DefineDirective{ "1", {} };
		defines_["__DBL_HAS_INFINITY__"] = DefineDirective{ "1", {} };
		defines_["__DBL_HAS_QUIET_NAN__"] = DefineDirective{ "1", {} };
		defines_["__DBL_IS_IEC_60559__"] = DefineDirective{ "1", {} };

		// Long Double (80-bit extended precision on x86_64)
		defines_["__LDBL_MANT_DIG__"] = DefineDirective{ "64", {} };
		defines_["__LDBL_DIG__"] = DefineDirective{ "18", {} };
		defines_["__LDBL_DECIMAL_DIG__"] = DefineDirective{ "21", {} };
		defines_["__LDBL_MIN_EXP__"] = DefineDirective{ "(-16381)", {} };
		defines_["__LDBL_MIN_10_EXP__"] = DefineDirective{ "(-4931)", {} };
		defines_["__LDBL_MAX_EXP__"] = DefineDirective{ "16384", {} };
		defines_["__LDBL_MAX_10_EXP__"] = DefineDirective{ "4932", {} };
		defines_["__LDBL_MIN__"] = DefineDirective{ "3.36210314311209350626267781732175260e-4932L", {} };
		defines_["__LDBL_MAX__"] = DefineDirective{ "1.18973149535723176502126385303097021e+4932L", {} };
		defines_["__LDBL_EPSILON__"] = DefineDirective{ "1.08420217248550443400745280086994171e-19L", {} };
		defines_["__LDBL_DENORM_MIN__"] = DefineDirective{ "3.64519953188247460252840593361941982e-4951L", {} };
		defines_["__LDBL_NORM_MAX__"] = DefineDirective{ "1.18973149535723176502126385303097021e+4932L", {} };
		defines_["__LDBL_HAS_DENORM__"] = DefineDirective{ "1", {} };
		defines_["__LDBL_HAS_INFINITY__"] = DefineDirective{ "1", {} };
		defines_["__LDBL_HAS_QUIET_NAN__"] = DefineDirective{ "1", {} };
		defines_["__LDBL_IS_IEC_60559__"] = DefineDirective{ "1", {} };

		// Width / word order / deprecation markers (GCC compatibility)
		defines_["__SCHAR_WIDTH__"] = DefineDirective{ "8", {} };
		defines_["__SHRT_WIDTH__"] = DefineDirective{ "16", {} };
		defines_["__INT_WIDTH__"] = DefineDirective{ "32", {} };
		defines_["__LONG_WIDTH__"] = DefineDirective{ settings_.getLongSizeBits() == 32 ? "32" : "64", {} };
		defines_["__LONG_LONG_WIDTH__"] = DefineDirective{ "64", {} };
		defines_["__PTRDIFF_WIDTH__"] = DefineDirective{ "64", {} };
		defines_["__SIG_ATOMIC_WIDTH__"] = DefineDirective{ "32", {} };
		defines_["__SIZE_WIDTH__"] = DefineDirective{ "64", {} };
		defines_["__WCHAR_WIDTH__"] = DefineDirective{ "32", {} };
		defines_["__WINT_WIDTH__"] = DefineDirective{ "32", {} };
		defines_["__INTPTR_WIDTH__"] = DefineDirective{ "64", {} };
		defines_["__INTMAX_WIDTH__"] = DefineDirective{ "64", {} };

		// Floating-point word order
		defines_["__FLOAT_WORD_ORDER__"] = DefineDirective{ "__BYTE_ORDER__", {} };

		// Deprecation marker
		defines_["__DEPRECATED"] = DefineDirective{ "__attribute__((deprecated))", {} };
		
		defines_["__FILE__"] = FunctionDirective{ [this]() -> std::string {
			// Use std::filesystem to normalize path separators for cross-platform compatibility
			// This converts backslashes to forward slashes on all platforms
			std::filesystem::path file_path(filestack_.top().file_name);
			std::string normalized_path = file_path.generic_string();
			return "\"" + normalized_path + "\"";
		} };
		defines_["__LINE__"] = FunctionDirective{ [this]() -> std::string {
			return std::to_string(filestack_.top().line_number);
		} };
		defines_["__COUNTER__"] = FunctionDirective{ [this]() -> std::string {
			return std::to_string(counter_value_++);
		} };

		defines_["__DATE__"] = FunctionDirective{ [] {
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm tm_now = localtime_safely(&time_t_now);
			char buffer[12];
			std::strftime(buffer, sizeof(buffer), "\"%b %d %Y\"", &tm_now);
			return std::string(buffer);
		} };

		defines_["__TIME__"] = FunctionDirective{ [] {
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm tm_now = localtime_safely(&time_t_now);
			char buffer[10];
			std::strftime(buffer, sizeof(buffer), "\"%H:%M:%S\"", &tm_now);
			return std::string(buffer);
		} };

		// __TIMESTAMP__ - file modification time
		defines_["__TIMESTAMP__"] = FunctionDirective{ [this]() -> std::string {
			if (!filestack_.empty()) {
				return filestack_.top().timestamp;
			}
			return "\"??? ??? ?? ??:??:?? ????\"";
		} };

		// __INCLUDE_LEVEL__ - nesting depth of includes (0 for main file)
		defines_["__INCLUDE_LEVEL__"] = FunctionDirective{ [this]() -> std::string {
			// Stack size - 1 because the main file is at level 0
			return std::to_string(filestack_.size() > 0 ? filestack_.size() - 1 : 0);
		} };

		// __FUNCTION__ (MSVC extension)
		defines_["__FUNCTION__"] = DefineDirective("__func__", {});

		// __nullptr (MSVC extension) - represents nullptr type for decltype(__nullptr)
		defines_["__nullptr"] = DefineDirective("nullptr", {});

		// __PRETTY_FUNCTION__ (GCC extension) and __func__ (C++11 standard)
		// These are NOT preprocessor macros - they are compiler builtins handled by the parser
		// The parser will replace them with string literals containing the current function name
		// when they appear inside a function body
		//

		defines_["__STDCPP_DEFAULT_NEW_ALIGNMENT__"] = FunctionDirective{ [] {
			constexpr std::size_t default_new_alignment = alignof(std::max_align_t);
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%zuU", default_new_alignment);
			return std::string(buffer);
		} };
	}

	struct ScopedFileStack {
		ScopedFileStack(std::stack<CurrentFile>& filestack, std::string_view file, long included_at_line = 0) : filestack_(filestack) {
			// Get file modification timestamp
			std::string timestamp_str;
			try {
				auto ftime = std::filesystem::last_write_time(file);
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
				auto time_t_now = std::chrono::system_clock::to_time_t(sctp);
				std::tm tm_now = localtime_safely(&time_t_now);
				char buffer[32];
				std::strftime(buffer, sizeof(buffer), "\"%a %b %d %H:%M:%S %Y\"", &tm_now);
				timestamp_str = buffer;
			} catch (...) {
				// If we can't get the timestamp, use a default
				timestamp_str = "\"??? ??? ?? ??:??:?? ????\"";
			}
			filestack_.push({ file, 0, timestamp_str, included_at_line });
		}
		~ScopedFileStack() {
			filestack_.pop();
		}

		std::stack<CurrentFile>& filestack_;
	};

	CompileContext& settings_;
	FileTree& tree_;
	std::unordered_map<std::string, Directive> defines_;
	std::unordered_set<std::string> proccessedHeaders_;
	std::stack<CurrentFile> filestack_;
	std::string result_;
	std::vector<std::string> file_paths_;  // Unique list of source file paths
	std::vector<SourceLineMapping> line_map_;  // Maps preprocessed lines to source locations
	size_t current_output_line_ = 1;  // Track current line number in preprocessed output
	size_t current_file_index_ = 0;  // Track current file index (updated when switching files)
	size_t current_parent_line_ = 0;  // Track the preprocessed line where current file was #included (0 for main)
	unsigned long long counter_value_ = 0;

	// State for tracking multiline raw string literals
	bool inside_multiline_raw_string_ = false;
	std::string multiline_raw_delimiter_;
};
