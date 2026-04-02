template<typename T>
struct ref_wrapper {
	using lref = T&;
	using collapsed = lref&&;

	static int set(collapsed value) {
		value = 42;
		return 0;
	}
};

int main() {
	int x = 0;
	ref_wrapper<int>::set(x);
	return x == 42 ? 0 : 1;
}
