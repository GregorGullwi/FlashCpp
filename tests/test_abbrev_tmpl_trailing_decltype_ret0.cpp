// Abbreviated function template with trailing decltype using parameter names
auto add(auto a, auto b) -> decltype(a + b) { return a + b; }

int main() {
	return add(3, 4) - 7; // 3+4-7 = 0
}
