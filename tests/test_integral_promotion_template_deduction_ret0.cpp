// C++20 [temp.deduct.call]: deduction sees the promoted int argument type.

template <class T>
int deducedSize(T) {
	return sizeof(T) == sizeof(int) ? 0 : 1;
}

int main() {
	unsigned char value = 200;
	return deducedSize(+value);
}
