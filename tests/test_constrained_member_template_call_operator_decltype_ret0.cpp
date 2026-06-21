// Regression: postfix callable resolution must defer/resolve constrained
// member-template operator() calls used inside decltype.

namespace constrained_call_operator_decltype {
	template<class T>
	T&& declval() noexcept;

	struct Synth {
		template <class T, class U>
		auto operator()(const T& left, const U& right) const
			requires requires {
				left < right;
				right < left;
			}
		{
			return 0;
		}
	};

	template<class T, class U = T>
	using result = decltype(Synth{}(declval<T&>(), declval<U&>()));

	result<int> make_result();
}

int main() {
	return 0;
}
