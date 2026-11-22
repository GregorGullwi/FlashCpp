// Identical type patterns remain distinct when their associated constraints differ.
template<class T> struct Select { static constexpr int value = 0; };
template<class T> requires (sizeof(T) == 1) struct Select<T*> { static constexpr int value = 1; };
template<class T> requires (sizeof(T) == sizeof(int)) struct Select<T*> { static constexpr int value = 2; };
int main() {
	return (Select<char*>::value != 1) + (Select<int*>::value != 2) + Select<double*>::value;
}
