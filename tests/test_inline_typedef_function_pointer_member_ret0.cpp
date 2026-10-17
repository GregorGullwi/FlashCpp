// Regression: an inline typedef record must accept function-pointer member
// declarators through the shared declarator grammar, exactly like a named
// record. The identifier-only inline member path used to reject the opening
// parenthesis with "Expected member name in struct".
typedef struct {
	int (*function)();
	int (*handler)(int);
	int value;
} Callbacks;

int answer() {
	return 42;
}

int identity(int value) {
	return value;
}

int main() {
	Callbacks callbacks{answer, identity, 5};
	if (callbacks.function() != 42) return 1;
	if (callbacks.handler(7) != 7) return 2;
	if (callbacks.value != 5) return 3;
	return 0;
}
