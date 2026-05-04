char32_t overwideChar32Return() {
	return 0x100000001ull;
}

int main() {
	return overwideChar32Return() == static_cast<char32_t>(1u) ? 0 : 1;
}
