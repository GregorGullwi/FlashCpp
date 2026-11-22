// A type-constraint on a partial specialization's parameter must be satisfied.
template<class T> concept IntSized = sizeof(T) == sizeof(int);
template<class T> struct Pick { static constexpr int value = 0; };
template<IntSized T> struct Pick<T*> { static constexpr int value = 1; };

int main() {
	return Pick<double*>::value + (Pick<int*>::value != 1);
}
