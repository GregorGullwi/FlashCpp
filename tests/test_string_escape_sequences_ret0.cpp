// Test that string literal length computation correctly handles escape sequences

int main() {
	// Simple string
	const char (&s1)[6] = "hello";  // 5 chars + null = 6
	
	// String with newline escape
	const char (&s2)[12] = "hello\nworld";  // "hello<newline>world" = 11 chars + null = 12
	
	// String with tab escape  
	const char (&s3)[7] = "hello\t";  // "hello<tab>" = 6 chars + null = 7
	
	// String with backslash escape
	const char (&s4)[2] = "\\";  // "\\" = 1 logical char + null = 2
	
	// String with hex escape
	const char (&s5)[2] = "\x41";  // \x41 (hex for 'A') = 1 char + null = 2
	
	// String with octal escape
	const char (&s6)[2] = "\101";  // \101 (octal for 'A') = 1 char + null = 2
	
	// String with unicode escape
	const char (&s7)[2] = "\u0041";  // \u0041 (unicode for 'A') = 1 char + null = 2
	
	// String with multiple escapes
	const char (&s8)[5] = "a\nb\t";  // "a<newline>b<tab>" = 4 chars + null = 5
	
	return 0;
}
