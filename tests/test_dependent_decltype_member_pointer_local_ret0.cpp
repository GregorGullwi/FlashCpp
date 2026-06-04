struct Box {
	int value;
};

template <class T>
struct RefWrap {
	T* ptr;

	T& get() {
		return *ptr;
	}
};

template <class Decayed, class Wrapped>
auto call(Decayed pmd, Wrapped wrapped) -> decltype(wrapped.get().*pmd) {
	return wrapped.get().*pmd;
}

template <class T>
struct IsReference {
	static constexpr bool value = false;
};

template <class T>
struct IsReference<T&> {
	static constexpr bool value = true;
};

int main() {
	Box box{42};
	RefWrap<Box> wrapped{&box};
	int Box::*member = &Box::value;
	using Result = decltype(call(member, wrapped));
	return IsReference<Result>::value ? 0 : 1;
}
