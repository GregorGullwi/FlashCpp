// Regression: C++20 permits omitted `typename` for dependent qualified types
// in requires-expression parameter declarations.

template<class T>
struct identity_traits {
	using type = T;
};

template<class T>
concept has_self_parameter = requires(identity_traits<T>::type value) {
	value;
};

int main() {
	return has_self_parameter<int> ? 0 : 1;
}
