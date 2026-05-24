#include <string_view>

int main() {
	std::string_view sv = "hello";
	std::string_view tail = sv.substr(1);
	return tail == "ello" ? 0 : 1;
}
