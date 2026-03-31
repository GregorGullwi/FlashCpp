// Test that template specializations declared in a namespace are properly
// tracked with namespace context.  The primary template and a partial
// specialization both live inside the "calc" namespace; instantiation
// happens from main() (global scope).  This exercises the declaration-site
// namespace resolution for both the primary and specialization paths in
// try_instantiate_class_template.

namespace calc {
	// Primary template: stores a value directly
template <typename T>
struct Holder {
	T value;
	T get() const { return value; }
};

	// Partial specialization for pointer types: dereferences
template <typename T>
struct Holder<T*> {
	T* ptr;
	T get() const { return *ptr; }
};

	// Free function using the primary template
int use_holder(Holder<int> h) {
	return h.get();
}

	// Free function using the pointer specialization
int use_ptr_holder(Holder<int*> h) {
	return h.get();
}
} // namespace calc

int main() {
	calc::Holder<int> h{30};
	int val = 12;
	calc::Holder<int*> hp{&val};
	return calc::use_holder(h) + calc::use_ptr_holder(hp); // 30 + 12 = 42
}
