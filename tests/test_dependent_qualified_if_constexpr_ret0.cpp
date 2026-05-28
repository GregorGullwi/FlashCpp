#include <type_traits>

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
	return Waiter<std::true_type>::run(&value);
}
