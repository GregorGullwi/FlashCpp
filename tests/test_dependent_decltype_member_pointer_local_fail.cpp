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

int main() {
	Box box{42};
	RefWrap<Box> wrapped{&box};
	int Box::*member = &Box::value;
	using Result = decltype(call(member, wrapped));
	Result* result = nullptr;
	return result == nullptr ? 0 : 1;
}
