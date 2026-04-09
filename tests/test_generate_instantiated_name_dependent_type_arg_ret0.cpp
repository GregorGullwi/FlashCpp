template <typename T>
struct Wrapper {
	static constexpr int tag = sizeof(T);
};

template <typename T>
struct Outer {
	static int getTag() {
		return Wrapper<T>::tag;
	}

	static int getPointerTag() {
		return Wrapper<T*>::tag;
	}
};

int main() {
	int tag_from_outer_int = Outer<int>::getTag();
	int tag_from_outer_double = Outer<double>::getTag();
	int direct_int_tag = Wrapper<int>::tag;
	int direct_double_tag = Wrapper<double>::tag;
	int pointer_tag = Outer<int>::getPointerTag();

	bool ok = tag_from_outer_int == sizeof(int) &&
			  tag_from_outer_double == sizeof(double) &&
			  direct_int_tag == sizeof(int) &&
			  direct_double_tag == sizeof(double) &&
			  pointer_tag == sizeof(int*);
	return ok ? 0 : 1;
}
