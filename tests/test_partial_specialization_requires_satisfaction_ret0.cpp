// A partial specialization participates only when its requires-clause is satisfied.
template<class T> struct Pick { static constexpr int value = 0; };
template<class T> requires (sizeof(T) != sizeof(int)) struct Pick<T*> { static constexpr int value = 1; };

int main() {
	return Pick<int*>::value + (Pick<char*>::value != 1);
}
