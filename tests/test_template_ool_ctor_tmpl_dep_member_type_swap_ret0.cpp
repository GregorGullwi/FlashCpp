// Regression: out-of-line constructor-template attachment for overloads where
// parameter positions swap between a concrete inner-template param (U) and a
// dependent outer-class member type (typename T::value_type).
//
// ctor 1: Container(U a,                typename T::value_type b)  -> result = a+b+10
// ctor 2: Container(typename T::value_type a, U b                ) -> result = a+b+20
//
// Container<Traits> a(100.0, (short)5)  calls ctor 1: 100+5+10 = 115
// Container<Traits> b((short)5, 100.0)  calls ctor 2:   5+100+20 = 125
// Return 0 on success.
template<typename T>
struct Container {
	int result = 0;
	template<typename U>
	Container(U a, typename T::value_type b);
	template<typename U>
	Container(typename T::value_type a, U b);
};

template<typename T>
template<typename U>
Container<T>::Container(U a, typename T::value_type b) : result((int)a + (int)b + 10) {}

template<typename T>
template<typename U>
Container<T>::Container(typename T::value_type a, U b) : result((int)a + (int)b + 20) {}

struct Traits { using value_type = short; };

int main() {
	Container<Traits> a(100.0, (short)5);
	Container<Traits> b((short)5, 100.0);
	return (a.result == 115 && b.result == 125) ? 0 : 1;
}
