struct Value {
	int value;
	int& operator+() & { return value; }
	const int& operator+() const & { return value; }
	short operator-() & { return value + 10; }
	long long operator-() && { return 91; }
};

int main() {
	Value object;
	object.value = 5;
	int& reference = +object;
	reference = 7;
	const Value& constant = object;
	if (+constant != 7 || object.value != 7 || -object != 17) return 1;
	if (-Value{} != 91) return 2;
	if (+Value{} != 0) return 3;
	return 0;
}
