// Test: struct identity uses the same TypeIndex for exact matches and keeps
// different TypeIndex values distinct through value, pointer, and reference conversions.

struct Foo {
	int value;
};

struct Bar {
	int value;
};

int pickValue(Foo value) {
	return value.value;
}

int pickValue(Bar value) {
	return value.value + 100;
}

int pickPointer(Foo* value) {
	return value->value;
}

int pickPointer(Bar* value) {
	return value->value + 100;
}

int pickReference(Foo& value) {
	return value.value;
}

int pickReference(Bar& value) {
	return value.value + 100;
}

int main() {
	Foo foo{3};
	Bar bar{5};
	Foo same = foo;

	if (same.value != 3)
		return 1;
	if (pickValue(foo) != 3)
		return 2;
	if (pickValue(bar) != 105)
		return 3;
	if (pickPointer(&foo) != 3)
		return 4;
	if (pickPointer(&bar) != 105)
		return 5;
	if (pickReference(foo) != 3)
		return 6;
	if (pickReference(bar) != 105)
		return 7;

	return 0;
}
