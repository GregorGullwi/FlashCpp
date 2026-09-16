// A deleted definition must be the first declaration; an out-of-line nested
// member function template definition is not.
template <typename T>
struct Host {
	template <typename U>
	void f();
};

template <typename T>
template <typename U>
void Host<T>::f() = delete;
