// Reduced regression for DiagnosticId::InvalidIntegerLiteralSuffix (1102).
// C++20 [lex.icon] only allows u, l, ul, lu, ll, ull, llu suffixes
// (case-insensitive); "UU" is none of them and must be diagnosed.
int main() {
	int x = 0UU;
	return x;
}
