struct ValueHolder {
	int value;

	ValueHolder(int* p)
		: value(*p) {}

	ValueHolder(int v)
		: value(v + 100) {}
};

int main() {
	int value = 7;
	ValueHolder holder(&value);
	return holder.value - 7;
}
