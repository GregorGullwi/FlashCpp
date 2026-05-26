// Regression: out-of-line member-function-template attachment for overloads
// where parameter positions swap between a concrete inner-template param (U)
// and a dependent outer-class member type (typename T::key_type / value_type).
//
// insert overload 1: insert(typename T::key_type key, U val)   -> result = key+val+10
// insert overload 2: insert(U key,                   typename T::value_type val) -> result = key+val+20
//
// Policy: key_type=int, value_type=unsigned char
//   m.insert((int)3, 4.0)          -> calls overload 1: 3+4+10=17
//   m2.insert(5.0, (unsigned char)6) -> calls overload 2: 5+6+20=31
// Return 0 on success.
struct Policy { using key_type = int; using value_type = unsigned char; };

template<typename T>
struct Map {
	int result = 0;
	template<typename U>
	void insert(typename T::key_type key, U val);
	template<typename U>
	void insert(U key, typename T::value_type val);
};

template<typename T>
template<typename U>
void Map<T>::insert(typename T::key_type key, U val) {
	result = (int)key + (int)val + 10;
}

template<typename T>
template<typename U>
void Map<T>::insert(U key, typename T::value_type val) {
	result = (int)key + (int)val + 20;
}

int main() {
	Map<Policy> m;
	m.insert((int)3, 4.0);
	if (m.result != 17) {
		return 1;
	}

	Map<Policy> m2;
	m2.insert(5.0, (unsigned char)6);
	if (m2.result != 31) {
		return 2;
	}

	return 0;
}
