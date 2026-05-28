// Regression: constexpr branch selection in nested class-template ctor/dtor
// should see instantiated template arguments for `_EntersWait::value`.

template<bool B>
struct bool_constant {
	static constexpr bool value = B;
};

int g_waiter_counter = 0;

template<typename PoolTag>
struct waiter_pool {
	template<typename _EntersWait>
	struct waiter {
		waiter() {
			if constexpr (_EntersWait::value) {
				g_waiter_counter += 7;
			} else {
				g_waiter_counter += 100;
			}
		}

		~waiter() {
			if constexpr (_EntersWait::value) {
				g_waiter_counter -= 7;
			} else {
				g_waiter_counter -= 100;
			}
		}
	};
};

int main() {
	{
		waiter_pool<int>::waiter<bool_constant<true>> w;
		if (g_waiter_counter != 7) {
			return 1;
		}
	}
	return g_waiter_counter;
}
