// Gate 0: typeid inside a function template must use the bound argument, not the
// parameter name. typeid(T) == typeid(T) is tautological and does not catch this.

template <typename T>
const void* typeIdOfType() {
	return typeid(T);
}

template <typename T>
const void* typeIdOfValue(T value) {
	return typeid(value);
}

struct ByteBox {
	unsigned char v;
};

struct WideBox {
	long long high;
	int low;
};

int main() {
	if (typeIdOfType<int>() != typeid(int)) return 1;
	if (typeIdOfType<char>() != typeid(char)) return 2;
	if (typeIdOfType<unsigned>() != typeid(unsigned)) return 3;
	if (typeIdOfType<long long>() != typeid(long long)) return 4;
	if (typeIdOfType<int>() == typeIdOfType<char>()) return 5;

	int i = 11;
	char c = 'Q';
	unsigned u = 9;
	long long ll = 1234567890123LL;
	if (typeIdOfValue(i) != typeid(int)) return 6;
	if (typeIdOfValue(c) != typeid(char)) return 7;
	if (typeIdOfValue(u) != typeid(unsigned)) return 8;
	if (typeIdOfValue(ll) != typeid(long long)) return 9;
	if (typeIdOfValue(i) != typeIdOfType<int>()) return 10;

	ByteBox byte_box{200};
	WideBox wide_box{99, 3};
	if (typeIdOfType<ByteBox>() != typeid(ByteBox)) return 11;
	if (typeIdOfType<WideBox>() != typeid(WideBox)) return 12;
	if (typeIdOfValue(byte_box) != typeid(ByteBox)) return 13;
	if (typeIdOfValue(wide_box) != typeid(WideBox)) return 14;
	if (typeIdOfType<ByteBox>() == typeIdOfType<WideBox>()) return 15;
	return 0;
}
