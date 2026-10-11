// A missing function name or 'operator' after '::' in an out-of-line member
// definition is reported as OutOfLineDefinitionMalformedTarget (1023).
struct S {};

int S::() {
	return 0;
}

int main() {
	return 0;
}
