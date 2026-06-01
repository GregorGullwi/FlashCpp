struct true_type {
	static constexpr bool value = true;
};

template<typename EnterPolicy>
struct Waiter {
	template<typename T>
	static int run(const T*) {
		if constexpr (EnterPolicy::value) {
			return 0;
		}
		return 1;
	}
};

int main() {
	int value = 0;
	return Waiter<true_type>::run(&value);
}
