template<class T> struct Owner {
	T field;
	T method(T value) const & noexcept {
		return value;
	}
};

template<class T> struct Key {};

template<class U> struct Key<U*>;

template<class T> struct Key<T*> {
	T Owner<T>::* object;
	T (Owner<T>::*method)(T) const & noexcept;
};

template<class T> struct Key<T&> {
	T Owner<T>::* object;
	T (Owner<T>::*method)(T) const & noexcept;
};

int main() {
	Key<short*> short_pointer{nullptr, nullptr};
	Key<long long*> wide_pointer{nullptr, nullptr};
	Key<short&> short_reference{nullptr, nullptr};
	Key<long long&> wide_reference{nullptr, nullptr};
	return sizeof(short_pointer) == sizeof(wide_pointer) && sizeof(short_reference) == sizeof(wide_reference) ? 0 : 1;
}
