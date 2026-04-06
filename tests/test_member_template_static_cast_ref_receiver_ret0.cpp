struct Counter {
int value;

Counter() : value(0) {}

template<typename T>
int bump() {
value += static_cast<int>(sizeof(T));
return value;
}
};

int main() {
	Counter counter;
	int cast_result = static_cast<Counter&>(counter).bump<int>();
	if (cast_result != static_cast<int>(sizeof(int)))
		return 1;
	if (counter.value != static_cast<int>(sizeof(int)))
		return 2;

	Counter left;
	Counter right;
	bool pick_left = false;
	int conditional_result = (pick_left ? left : right).bump<int>();
	if (conditional_result != static_cast<int>(sizeof(int)))
		return 3;
	if (left.value != 0)
		return 4;
	if (right.value != static_cast<int>(sizeof(int)))
		return 5;
	return 0;
}
