template<class T, class U> concept Bigger = sizeof(T) > sizeof(U);
template<class T, class U> struct Pair {};
template<class T> struct Pick { static constexpr int value = 0; };
template<class U, Bigger<U> T> struct Pick<Pair<T, U>*> { static constexpr int value = 1; };
int main() {
	return Pick<Pair<char, int>*>::value + (Pick<Pair<int, char>*>::value != 1);
}
