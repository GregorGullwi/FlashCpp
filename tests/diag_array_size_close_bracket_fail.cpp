// Reduced regression for DiagnosticId::ExpectedCloseBracketAfterArraySize (1003)
// with its attached NoteToMatchOpeningBracket note. The array-size type-id
// must diagnose the missing ']' at the offending token and point back at the
// opening bracket.
int main() {
	int n = sizeof(int[3 4]);
	return n;
}

// expected-diag: error ExpectedCloseBracketAfterArraySize#1003 6:23
// expected-diag: note NoteToMatchOpeningBracket#1051 6:20
