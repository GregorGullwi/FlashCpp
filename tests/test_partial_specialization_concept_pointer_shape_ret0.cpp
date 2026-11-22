// A deduced pointer keeps its pointer shape when a concept reads sizeof(T).
template<class T> concept IntSized = sizeof(T) == sizeof(int);
template<class T> struct Pick { static constexpr int value = 0; };
template<IntSized T> struct Pick<T*> { static constexpr int value = 1; };

template<class T> struct RequiresPick { static constexpr int value = 0; };
template<class T> requires (sizeof(T) == sizeof(int)) struct RequiresPick<T*> { static constexpr int value = 2; };

int main() {
	return (Pick<int*>::value != 1) + Pick<int**>::value +
		(RequiresPick<int*>::value != 2) + RequiresPick<int**>::value;
}
