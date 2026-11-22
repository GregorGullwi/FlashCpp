struct Yes { static constexpr bool value = true; };
struct No { static constexpr bool value = false; };
template<class T> struct Pick { static constexpr int value = 0; };
template<class T> requires (T::value) struct Pick<T*> { static constexpr int value = 1; };
int main() {
	return Pick<No*>::value + (Pick<Yes*>::value != 1);
}
