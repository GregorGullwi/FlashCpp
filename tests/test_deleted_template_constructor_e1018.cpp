// A deleted out-of-line constructor definition of a class template is not a
// first declaration.
template <typename T>
struct Host {
	Host();
};

template <typename T>
Host<T>::Host() = delete;
