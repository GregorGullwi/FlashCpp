template <typename T>
struct Provider {
	struct Node {
		template <typename U>
		using Apply = T;
	};
};

template <typename T>
struct Use {
	template <typename U>
	using Alias = typename Provider<T>::Node::template Apply<U>;

	using Ref = Alias<int>&;
	using Ptr = Alias<int>*;
};

template <typename T>
struct is_lvalue_reference {
	static constexpr bool value = false;
};

template <typename T>
struct is_lvalue_reference<T&> {
	static constexpr bool value = true;
};

template <typename T>
struct is_pointer {
	static constexpr bool value = false;
};

template <typename T>
struct is_pointer<T*> {
	static constexpr bool value = true;
};

template <typename T>
struct is_function {
	static constexpr bool value = false;
};

template <typename R, typename... Args>
struct is_function<R(Args...)> {
	static constexpr bool value = true;
};

using Fn = int(char);

static_assert(is_lvalue_reference<Use<int>::Ref>::value);
static_assert(is_pointer<Use<int>::Ptr>::value);
static_assert(is_function<Use<Fn>::Alias<int>>::value);

int main() {
	return is_lvalue_reference<Use<int>::Ref>::value &&
			is_pointer<Use<int>::Ptr>::value &&
			is_function<Use<Fn>::Alias<int>>::value
		? 0
		: 1;
}
