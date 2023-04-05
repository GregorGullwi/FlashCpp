#include <string_view>
#include <map>
#include <vector>
#include <variant>

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

			if (arg.size() >= 2 && arg[0] == '-') {
				if (arg.size() >= 3 && arg[1] == '-') {
					// Option with long name, e.g. --option=value
					auto equal_pos = arg.find('=');
					if (equal_pos == std::string_view::npos) {
						optionValues_[arg.substr(2)] = std::monostate{};
					}
					else {
						optionValues_[arg.substr(2, equal_pos - 2)] = arg.substr(equal_pos + 1);
					}
				}
				else {
					// Option with short name, e.g. -o value
					if (arg == "-I" && i + 1 < argc) {
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

private:
	std::map<std::string_view, std::variant<std::monostate, std::string_view>> optionValues_;
	std::vector<std::string_view> inputFileArgs_;
};
