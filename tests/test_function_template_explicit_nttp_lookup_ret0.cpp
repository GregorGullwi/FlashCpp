namespace detail {
	template<bool UseFast = false, class T>
	bool compare(T* value, T& expected, T desired) {
		*value = desired;
		expected = desired;
		return UseFast;
	}

	template<bool UseFast = false, class T>
	bool compareWrapper(T* value, T& expected, T desired) {
		return detail::compare<UseFast>(value, expected, desired);
	}
}

int main() {
	int value = 1;
	int expected = 0;
	return detail::compareWrapper<true>(&value, expected, 42) && value == 42 && expected == 42 ? 0 : 1;
}
