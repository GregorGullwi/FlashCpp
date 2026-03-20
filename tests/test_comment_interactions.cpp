// Comprehensive test for comment interaction edge cases in the preprocessor.
// Each section returns a distinct error code on failure so we can tell which
// case broke.

// ---- 1. /* inside a // comment must not trigger block-comment mode ----
int case1_a() { return 1; } // comment with /* in it
int case1_b() { return 2; } // this line would be swallowed without the fix

// ---- 2. // inside a /* */ block comment must not truncate the line ----
int case2_a() { return 10; } /* see http://example.com */ int case2_b() { return 20; }
int case2_c() { return 30; } // this line would be swallowed without the fix

// ---- 3. Multi-line block comment containing // ----
int case3_a() { return 100; }
/* block comment
   with // slashes inside
   and more text */
int case3_b() { return 200; }

// ---- 4. Multiple block comments on one line, second contains // ----
int case4() { return /* first */ 1 /* http://x */ + 2; }

// ---- 5. Block comment between code, no // involved (sanity check) ----
int case5() { return 5 /* nothing special */ + 5; }

// ---- 6. // in a string literal must not be treated as a comment ----
int case6() { const char* url = "http://example.com"; return url[0] == 'h' ? 1 : 0; }

// ---- 7. /* in a string literal must not start a block comment ----
int case7() { const char* s = "/* not a comment */"; return s[0] == '/' ? 1 : 0; }

// ---- 8. // after a block comment on the same line ----
int case8_val = 42; /* block */ // line comment after block
int case8() { return case8_val; }

// ---- 9. Nested-looking block comments (/* ... /* ... */) ----
// C++ doesn't nest block comments; the first */ closes the comment.
int case9_a() { return 7; }
/* outer /* still one comment */ int case9_b() { return 8; }

// ---- 10. Empty block comment ----
int case10() { return /**/1; }

// ---- 11. Block comment ending right before // on same line ----
int case11_val = 99; /* comment */ // another comment
int case11() { return case11_val; }

// ---- 12. // inside a char literal ----
// '/' followed by '/' in adjacent char literals — not a comment
int case12() {
	char a = '/';
	char b = '/';
	return (a == '/' && b == '/') ? 1 : 0;
}

int main() {
	// 1. /* inside // comment
	if (case1_a() + case1_b() != 3) return 1;

	// 2. // inside /* */ block comment
	if (case2_a() + case2_b() + case2_c() != 60) return 2;

	// 3. Multi-line block comment with //
	if (case3_a() + case3_b() != 300) return 3;

	// 4. Multiple block comments, second has //
	if (case4() != 3) return 4;

	// 5. Plain block comment (sanity)
	if (case5() != 10) return 5;

	// 6. // in string literal
	if (case6() != 1) return 6;

	// 7. /* in string literal
	if (case7() != 1) return 7;

	// 8. // after block comment on same line
	if (case8() != 42) return 8;

	// 9. Nested-looking block comment
	if (case9_a() + case9_b() != 15) return 9;

	// 10. Empty block comment
	if (case10() != 1) return 10;

	// 11. Block comment then // on same line
	if (case11() != 99) return 11;

	// 12. / in char literals not a comment
	if (case12() != 1) return 12;

	return 0;
}
