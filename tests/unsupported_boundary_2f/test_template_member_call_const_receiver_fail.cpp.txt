template<typename Value>
int callThroughConst(const Value& value) {
	return value.bump(41);
}

struct MutableOnly {
	int bump(int input) {
		return input + 1;
	}
};

int main() {
	MutableOnly value;
	return callThroughConst(value);
}
