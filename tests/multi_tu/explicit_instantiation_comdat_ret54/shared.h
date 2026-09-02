// Explicit instantiations stay mergeable COMDATs, matching MSVC and Clang.
// Two TUs may both emit the same specialization; the linker must merge them.

template <typename T>
T bump(T value) {
	return value + static_cast<T>(1);
}

template <typename T>
struct StrongBox {
	T value;
	void set(T v) { value = v; }
	T get() const { return value; }
};

int firstBump();
int firstBox();
