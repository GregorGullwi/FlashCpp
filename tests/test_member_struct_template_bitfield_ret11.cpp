template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		int a : 3;
		U b;
	};
};

int main() {
	return 11;
}
