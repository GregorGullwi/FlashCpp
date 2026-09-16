// An out-of-line '= default' on an ordinary member function of a class
// template is ill-formed.
template <typename T>
struct Host {
	void f();
};

template <typename T>
void Host<T>::f() = default;
