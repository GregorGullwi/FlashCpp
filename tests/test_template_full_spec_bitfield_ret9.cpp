template<typename T>
struct S;

template<>
struct S<int> {
	int a : 3;
	int b;
};

int main() {
	S<int> s{};
	s.b = 9;
	return s.b;
}
