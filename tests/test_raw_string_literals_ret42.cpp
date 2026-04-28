int main() {
	const char (&plain_size)[6] = R"(hello)";
	const char (&delimited_size)[6] = R"tag(hello)tag";
	const char (&escape_text_size)[13] = R"(hello\nworld)";
	const char* plain = R"(hello)";
	const char* delimited = R"tag(hello)tag";
	const char* escape_text = R"(hello\nworld)";
	const char* p = R"delim(a\nb)delim";
	if (plain[0] != 'h' || plain[4] != 'o' || plain[5] != 0) {
		return 1;
	}
	if (delimited[0] != 'h' || delimited[5] != 0) {
		return 2;
	}
	if (escape_text[5] != '\\' || escape_text[6] != 'n' || escape_text[12] != 0) {
		return 3;
	}
	if (p[0] != 'a' || p[1] != '\\' || p[2] != 'n' || p[3] != 'b' || p[4] != 0) {
		return 4;
	}
	return 42;
}
