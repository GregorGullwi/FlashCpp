namespace detail {
	template<typename T>
	struct Iter {
		T value;
	};

	template<typename T>
	long operator-(const Iter<T>& lhs, const Iter<T>& rhs) {
		return lhs.value - rhs.value;
	}
}

int main() {
	detail::Iter<long> last{9};
	detail::Iter<long> first{4};
	return ((last - first) == 5L) ? 0 : 1;
}
