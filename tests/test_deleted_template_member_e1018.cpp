// A deleted definition must be the first declaration; an out-of-line member
// definition of a class template is not.
template <typename T>
struct Host {
	void f();
};

template <typename T>
void Host<T>::f() = delete;
