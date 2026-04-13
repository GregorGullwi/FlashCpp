struct S {
	template<typename T, typename U>
	int f(T) {
		return 42;
	}
};

int main() {
	S s;
	return s.f(1);
}
