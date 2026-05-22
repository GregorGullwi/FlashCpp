int main() {
	__declspec(align(16)) int x = 40;
	[[maybe_unused]] __declspec(align(16)) int y = 2;
	return x + y;
}
