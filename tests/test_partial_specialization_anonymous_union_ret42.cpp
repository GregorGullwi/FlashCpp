// Anonymous-union members in a selected class-template partial specialization
// must be parsed and published with the same field schema as an ordinary class.
template<class T>
struct Partial;

template<class T>
struct Partial<T*> {
	union {
		T first;
		T second;
	};
	unsigned char tag;
};

int main() {
	Partial<long long*> value{};
	value.first = 7;
	if (value.second != 7) return 1;
	if (sizeof(Partial<long long*>) != 16) return 2;
	value.tag = 3;
	return value.tag == 3 && value.first == 7 ? 42 : 3;
}
