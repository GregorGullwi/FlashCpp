struct Box {
	int value;
};

template <class T>
struct is_lvalue_ref {
	static constexpr bool value = false;
};

template <class T>
struct is_lvalue_ref<T&> {
	static constexpr bool value = true;
};

template <class T>
struct RefWrap {
	T* ptr;

	T& get() {
		return *ptr;
	}
};

template <class Wrapped>
auto callWrapped(Wrapped wrapped) -> decltype(wrapped.get()) {
	return wrapped.get();
}

int main() {
	Box box{42};
	RefWrap<Box> wrapped{&box};
	using Result = decltype(callWrapped(wrapped));
	return is_lvalue_ref<Result>::value ? 0 : 1;
}
