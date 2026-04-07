template<typename T>
struct Traits {
	using ptr = T*;
	using ref = T&;
	using cv_value = const T;
};

template<typename T>
int compose(typename Traits<T>::ptr* pp,
			typename Traits<T>::ref ref,
			volatile typename Traits<T>::cv_value* cvp) {
	ref += **pp;
	return ref == 10 && *cvp == 7 ? 0 : 1;
}

int main() {
	int value = 5;
	int* ptr = &value;
	const volatile int cv_local = 7;
	return compose<int>(&ptr, value, &cv_local);
}
