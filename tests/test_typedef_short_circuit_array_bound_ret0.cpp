int runtimeValue();

typedef int OneInt[true || runtimeValue()];
typedef short ThreeShorts[(false && runtimeValue()) ? 2 : 3];

int runtimeValue() {
	return 1;
}

int main() {
	if (sizeof(OneInt) != sizeof(int[1]) || !__is_same(OneInt, int[1])) {
		return 1;
	}
	return sizeof(ThreeShorts) == sizeof(short[3]) && __is_same(ThreeShorts, short[3]) ? 0 : 2;
}
