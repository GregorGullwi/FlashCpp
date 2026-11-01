template<class T> struct TypeSource { using type = T; };
template<class T> struct NestedSource { using type = TypeSource<T>; };

template<class T> struct ValueHolder {
	typename T::type value;
};

template<class T> struct NestedHolder {
	typename T::type::type value;
};

struct Payload { short first; long long second; };

int main() {
	ValueHolder<TypeSource<char>> small;
	ValueHolder<TypeSource<double>> floating;
	ValueHolder<TypeSource<Payload>> record;
	NestedHolder<NestedSource<long long>> nested;
	small.value = 7;
	floating.value = 3.5;
	record.value.first = 9;
	record.value.second = 5000000000LL;
	nested.value = 6000000000LL;
	if (sizeof(small) != sizeof(char) || sizeof(floating) != sizeof(double) || sizeof(record) != sizeof(Payload)) return 1;
	if (small.value != 7 || floating.value != 3.5 || record.value.first != 9) return 2;
	if (record.value.second != 5000000000LL || nested.value != 6000000000LL) return 3;
	return 0;
}
