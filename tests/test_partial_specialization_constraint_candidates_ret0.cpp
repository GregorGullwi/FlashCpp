template<class T> struct Pick { static constexpr int value = 0; };
template<class T> struct Pick<T*> { static constexpr int value = 1; };
template<class T> requires (sizeof(T) != sizeof(int)) struct Pick<T**> { static constexpr int value = 2; };
int main() {
	return (Pick<int**>::value != 1) + (Pick<char**>::value != 2);
}
