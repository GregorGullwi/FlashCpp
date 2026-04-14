// Phase 2 validation: dependent enable_if<..., T>::type in return-type position
// Exercises the parser's ability to handle a dependent qualified member type
// (typename enable_if<!is_pointer<T>::value, T>::type) as a function return
// type.  The negation of the bool trait via `!` adds an extra layer of
// dependent expression evaluation inside the non-type argument.

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
	using type = T;
};

template<typename T>
struct is_pointer {
	static constexpr bool value = false;
};

template<typename T>
struct is_pointer<T*> {
	static constexpr bool value = true;
};

template<typename T>
typename enable_if<!is_pointer<T>::value, T>::type identity(T x) {
	return x;
}

int main() {
	return identity(5);
}
