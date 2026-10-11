// The address-of target get<int> returns const int&, but the declared
// member-function pointer returns int&. [conv.mem] requires the callable's
// function type to be identical (only the owner class may change), so the
// initialization must be rejected instead of silently storing a mismatched
// pointer. Recovered from the boundary-2F archive.
template<typename T>
struct Box {
	T value;

	template<typename U>
	const T& get();
};

template<typename T>
template<typename U>
const T& Box<T>::get() {
	return value;
}

int& (Box<int>::*pmf)() = &Box<int>::template get<int>;

int main() {
	return 0;
}
