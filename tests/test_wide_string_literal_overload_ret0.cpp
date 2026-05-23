int pick(const char*) {
	return 1;
}

int pick(const wchar_t*) {
	return 2;
}

int main() {
	return pick(L"hello") == 2 ? 0 : 1;
}
