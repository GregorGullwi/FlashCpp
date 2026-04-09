namespace std {
	typedef unsigned long size_t;

	template<bool _Value>
	struct __bool_constant {
		static constexpr bool value = _Value;
	};

	using true_type = __bool_constant<true>;
	using false_type = __bool_constant<false>;

	template<typename _Tp>
	_Tp&& declval() noexcept;

	template<typename _Tp>
	void swap(_Tp&, _Tp&) noexcept;

	template<typename _Tp, size_t _Nm>
	void swap(_Tp (&)[_Nm], _Tp (&)[_Nm]) noexcept;

	namespace __swappable_details {
		using std::swap;

		struct __do_is_swappable_impl {
			template<typename _Tp, typename = decltype(swap(std::declval<_Tp&>(), std::declval<_Tp&>()))>
			static true_type __test(int);

			template<typename>
			static false_type __test(...);
		};

		struct __do_is_nothrow_swappable_impl {
			template<typename _Tp>
			static __bool_constant<
				noexcept(swap(std::declval<_Tp&>(), std::declval<_Tp&>()))
			> __test(int);

			template<typename>
			static false_type __test(...);
		};
	}
}

int main() {
	return 0;
}
