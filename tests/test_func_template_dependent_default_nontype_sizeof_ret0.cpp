struct Pair {
	int a;
	double b;
};

template<typename Type, int Size = sizeof(Type)>
int getSize(Type) {
	return Size;
}

int main() {
	Pair value{1, 2.0};
	return getSize(value) == static_cast<int>(sizeof(Pair)) ? 0 : 1;
}
