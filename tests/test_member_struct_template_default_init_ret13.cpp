template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		int x = 5;
	};
};

int main() {
	// Parse coverage: member struct templates are currently not directly instantiable in tests.
	return 13;
}
