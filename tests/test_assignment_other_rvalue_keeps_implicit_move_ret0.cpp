struct Other { short value; };

struct Value {
	long long value;
	Value& operator=(Other&& other) { value = other.value; return *this; }
};

int main() {
	Value source;
	Value destination;
	source.value = 42;
	destination.value = 1;
	destination = static_cast<Value&&>(source);
	if (destination.value != 42) return 1;
	destination = Other{17};
	return destination.value == 17 ? 0 : 2;
}
