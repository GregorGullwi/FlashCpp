int pickString(const char*) {
	return 1;
}

int pickString(const wchar_t*) {
	return 2;
}

int pickString(const char8_t*) {
	return 4;
}

int pickString(const char16_t*) {
	return 8;
}

int pickString(const char32_t*) {
	return 16;
}

int pickChar(char) {
	return 3;
}

int pickChar(wchar_t) {
	return 5;
}

int pickChar(char8_t) {
	return 7;
}

int pickChar(char16_t) {
	return 11;
}

int pickChar(char32_t) {
	return 13;
}

int main() {
	if (pickString("a") != 1) {
		return 1;
	}
	if (pickString(L"a") != 2) {
		return 2;
	}
	if (pickString(u8"a") != 4) {
		return 3;
	}
	if (pickString(u"a") != 8) {
		return 4;
	}
	if (pickString(U"a") != 16) {
		return 5;
	}
	if (pickChar('a') != 3) {
		return 6;
	}
	if (pickChar(L'a') != 5) {
		return 7;
	}
	if (pickChar(u8'a') != 7) {
		return 8;
	}
	if (pickChar(u'a') != 11) {
		return 9;
	}
	if (pickChar(U'a') != 13) {
		return 10;
	}
	return 0;
}
