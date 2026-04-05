constexpr int value = 41;

consteval int readGlobalValue() {
	return value + 1;
}

int main() {
	int value = 1;
	(void)value;
	return readGlobalValue() == 42 ? 0 : 1;
}
