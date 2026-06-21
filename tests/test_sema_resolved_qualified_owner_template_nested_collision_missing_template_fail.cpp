template <class T>
struct Outer {
	template <class U>
	struct Base {
		template <class V>
		static int pick() {
			return 38 + static_cast<int>(sizeof(V));
		}
	};

	static int run() {
		return Base<T>::pick<int>();
	}
};

int main() {
	return Outer<int>::run();
}
