#include <string_view>

int main() {
	std::wstring_view sv = L"Hello";
	return sv.find(L'e') == 1 ? 0 : 1;
}
