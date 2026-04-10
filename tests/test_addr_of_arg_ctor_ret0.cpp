struct ValueHolder {
	int value;

	ValueHolder(int* p)
		: value(*p) {}

	ValueHolder(void* unused)
		: value(107) {}
};

int main() {
	int value = 7;
	ValueHolder holder(&value);
	return holder.value - 7;
}
