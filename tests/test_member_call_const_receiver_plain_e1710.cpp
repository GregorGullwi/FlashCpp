// Non-template form of the const-receiver member call. The parser owns this
// rejection directly (the template forms are replayed through instantiation),
// so this pins the diagnostic on the concrete named-member path.

struct MutableOnly {
	int bump(int input) {
		return input + 1;
	}
};

int useConst(const MutableOnly& value) {
	return value.bump(41);
}

int main() {
	MutableOnly value;
	return useConst(value);
}
