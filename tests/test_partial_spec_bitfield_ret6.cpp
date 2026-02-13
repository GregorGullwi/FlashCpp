template<typename T>
struct S {
	int value;
};

template<typename T>
struct S<T*> {
	int a : 3;
	int b;
};

int main() {
	S<int*> s{};
	s.b = 6;
	return s.b;
}
