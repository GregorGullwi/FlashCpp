template<class T>
struct TypedBits {
	T value : 3;
};

template<unsigned Width>
struct SizedBits {
	int value : Width;
};

int main() {
	TypedBits<unsigned> typed{5};
	SizedBits<4> sized{7};
	return typed.value == 5 && sized.value == 7 ? 0 : 1;
}
