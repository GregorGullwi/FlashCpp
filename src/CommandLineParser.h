#include <string_view>
#include <map>
#include <vector>
#include <variant>
#include <iostream>
#include <algorithm>

// ---- Unified command-line option registry ----
// Single source of truth for all supported options.
// Both the consteval _opt literal and isKnownFlag/printHelp derive from this table.

struct CliOptionInfo {
	std::string_view name;         // option name without leading '-'
	std::string_view value_hint;   // non-empty when the option takes a value (e.g. "<file>")
	std::string_view description;  // human-readable description for --help
	bool             is_alias;     // true for duplicate/alias entries omitted from --help output
};

// Options that accept a value (--name=value or -name value)
inline constexpr CliOptionInfo all_cli_value_options[] = {
	{ "o",          "<file>",   "Specify output object file",                                         false },
	{ "I",          "<path>",   "Add include directory",                                              false },
	{ "log-level",  "<level>",  "Set log level (see below for details)",                               false },
	{ "fmangling",  "<style>",  "Name mangling style: msvc or itanium",                              false },
};

// Flag options (no value, presence implies true)
inline constexpr CliOptionInfo all_cli_flags[] = {
	{ "h",                          "", "Show this help message",                                    true  },
	{ "help",                       "", "Show this help message",                                    false },
	{ "v",                          "", "Verbose output (shows dependency analysis and IR)",         false },
	{ "verbose",                    "", "Verbose output",                                            true  },
	{ "E",                          "", "Preprocess only (output preprocessed source)",              false },
	{ "d",                          "", "Enable debug output",                                       false },
	{ "debug",                      "", "Enable debug output",                                       true  },
	{ "perf-stats",                 "", "Show performance statistics",                               false },
	{ "stats",                      "", "Show performance statistics",                               true  },
	{ "time",                       "", "Show compilation timing",                                   false },
	{ "timing",                     "", "Show compilation timing",                                   true  },
	{ "fno-access-control",         "", "Disable access-control checks",                             false },
	{ "no-access-control",          "", "Disable access-control checks",                             true  },
	{ "fgcc-compat",                "", "Use GCC/Clang compatible built-in macros",                  false },
	{ "fclang-compat",              "", "Use GCC/Clang compatible built-in macros",                  true  },
	{ "fno-exceptions",             "", "Disable exception handling",                                false },
	{ "eager-template-instantiation", "", "Instantiate all template members eagerly (default: lazy)", false },
};

// Compile-time lookup: _opt user-defined literal.
// Returns the option name as a std::string_view; triggers a compile error for unknown names.
consteval std::string_view operator""_opt(const char* s, size_t len) {
	std::string_view sv(s, len);
	for (const auto& entry : all_cli_value_options)
		if (entry.name == sv) return sv;
	for (const auto& entry : all_cli_flags)
		if (entry.name == sv) return sv;
	throw "unrecognized option name";
}

class CommandLineParser {
public:
	CommandLineParser(int argc, char* argv[], CompileContext& context) {
		bool inQuotedString = false;
		char quoteChar = '\0';
		std::string quotedString;

		for (int i = 1; i < argc; i++) {
			std::string_view arg = argv[i];

			if (inQuotedString) {
				quotedString += " ";
				quotedString += arg;

				if (arg.back() == quoteChar) {
					inQuotedString = false;
					context.addIncludeDir(quotedString.substr(0, quotedString.size() - 1));
					quotedString.clear();
				}
				continue;
			}

			// On Windows, allow both - and / for options
			// On Unix/Linux, only allow - (to avoid treating absolute paths like /home/... as options)
			bool isOption = false;
			#if defined(_WIN32) || defined(_WIN64)
				isOption = arg.size() >= 2 && (arg[0] == '-' || arg[0] == '/');
			#else
				isOption = arg.size() >= 2 && arg[0] == '-';
			#endif

			if (isOption) {
				// Handle both - and / prefixes for options (Windows compatibility)
				if (arg.size() >= 3 && arg[1] == '-') {
					// Option with long name, e.g. --option=value (only with -)
					auto equal_pos = arg.find('=');
					if (equal_pos == std::string_view::npos) {
						optionValues_[arg.substr(2)] = std::monostate{};
					}
					else {
						optionValues_[arg.substr(2, equal_pos - 2)] = arg.substr(equal_pos + 1);
					}
				}
				else {
					// Option with short name, e.g. -I value or /I value
					// Also handle concatenated format: /Ipath or -Ipath
					std::string_view option_part = arg.substr(1);
					
					// Check if this is an include directory flag
					if (option_part.size() >= 1 && option_part[0] == 'I') {
						if (option_part.size() == 1) {
							// Format: /I path (space-separated)
							if (i + 1 < argc) {
								std::string_view nextArg = argv[++i];

								if (nextArg.front() == '"' || nextArg.front() == '\'') {
									inQuotedString = true;
									quoteChar = nextArg.front();
									quotedString = nextArg.substr(1);
									if (nextArg.back() == quoteChar) {
										inQuotedString = false;
										context.addIncludeDir(quotedString.substr(0, quotedString.size() - 1));
										quotedString.clear();
									}
								}
								else {
									context.addIncludeDir(nextArg);
								}
							}
						} else {
							// Format: /Ipath (concatenated)
							std::string_view path = option_part.substr(1);
							context.addIncludeDir(path);
						}
					}
					else if (isKnownFlag(arg.substr(1))) {
						// Known flags don't take a value
						optionValues_[arg.substr(1)] = std::monostate{};
					}
					else if (i + 1 >= argc) {
						optionValues_[arg.substr(1)] = std::monostate{};
					}
					else {
						optionValues_[arg.substr(1)] = argv[++i];
					}
				}
			}
			else {
				inputFileArgs_.push_back(arg);
			}
		}
	}

	bool hasOption(std::string_view optionName) const {
		return optionValues_.count(optionName) > 0;
	}

	bool hasFlag(std::string_view flagName) const {
		return optionValues_.count(flagName) > 0 && std::holds_alternative<std::monostate>(optionValues_.at(flagName)) == true;
	}

	std::variant<std::monostate, std::string_view> optionValue(std::string_view optionName) const {
		auto it = optionValues_.find(optionName);
		if (it != optionValues_.end()) {
			return it->second;
		}
		return std::monostate{};
	}

	const std::vector<std::string_view>& inputFileArgs() const {
		return inputFileArgs_;
	}

	// Print help text to stdout, derived from the constexpr option tables.
	static void printHelp() {
		static constexpr size_t kHelpColumnWidth = 36;

		std::cout << "FlashCpp - A C++20 compiler front-end\n\n"
		             "Usage: FlashCpp [options] <input-file>\n\n"
		             "Options:\n";

		auto print_option = [](const CliOptionInfo& opt) {
			if (opt.is_alias) return;

			std::string left = "  -";
			if (opt.name.size() > 1) left += '-';
			left += opt.name;

			if (!opt.value_hint.empty()) {
				// Long options (--) only support =value syntax; short options (-) use space
				left += (opt.name.size() > 1) ? '=' : ' ';
				left += opt.value_hint;
			}

			if (left.size() < kHelpColumnWidth) {
				left.append(kHelpColumnWidth - left.size(), ' ');
			} else {
				left += ' '; // Ensure at least one space separator
			}
			std::cout << left << opt.description << "\n";
		};

		for (const auto& opt : all_cli_value_options) {
			print_option(opt);
		}
		for (const auto& opt : all_cli_flags) {
			print_option(opt);
		}

		// Build log-level detail block from the constexpr tables in Log.h
		std::cout << "\nLog levels: ";
		bool first = true;
		for (const auto& lv : FlashCpp::all_log_levels) {
			if (!first) std::cout << '/';
			std::cout << lv.name;
			first = false;
		}
		std::cout << "\nLog categories: ";
		first = true;
		for (const auto& cat : FlashCpp::all_log_categories) {
			if (!first) std::cout << ", ";
			std::cout << cat.name;
			first = false;
		}
		std::cout << "\nUsage: --log-level=<level> or --log-level=<category>:<level>\n\n";
	}

private:
	// Helper to check if an option is a known flag (doesn't take a value).
	// Derived from the all_cli_flags constexpr table.
	static bool isKnownFlag(std::string_view flag) {
		return std::ranges::any_of(all_cli_flags, [flag](const auto& entry) { return entry.name == flag; });
	}

	std::map<std::string_view, std::variant<std::monostate, std::string_view>> optionValues_;
	std::vector<std::string_view> inputFileArgs_;
};
