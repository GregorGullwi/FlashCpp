template<class T> struct Value {
	T value;
	int operator-() const { return -value; }
	int read() const { return value; }
};
template<class T> struct Child : Value<T> {};
Child<short> makeShort() { Child<short> result; result.value = 27; return result; }
Child<int> makeInt() { Child<int> result; result.value = 29; return result; }
struct Virtual : virtual Value<short> {};
int receiver_calls = 0;
Child<short>* choose(Child<short>* pointer) { ++receiver_calls; return pointer; }
int main() {
	Child<short> aggregate{{13}};
	if (aggregate.value != 13) return 11;
	if (-aggregate != -13) return 1;
	if (-Child<short>{{17}} != -17) return 2;
	if (-makeShort() != -27) return 3;
	if (-makeInt() != -29) return 4;
	Child<short>& reference = aggregate;
	if (-reference != -13) return 5;
	Child<short>* pointer = &aggregate;
	if (-*pointer != -13) return 6;
	Virtual virtual_value;
	virtual_value.value = 31;
	if (-virtual_value != -31) return 7;
	if (pointer->read() != 13) return 8;
	if (-*choose(pointer) != -13 || receiver_calls != 1) return 9;
	using ShortPointer = Child<short>*;
	ShortPointer alias_pointer = pointer;
	if (-*alias_pointer != -13) return 10;
	Virtual* virtual_pointer = &virtual_value;
	if (-*virtual_pointer != -31) return 12;
	return 0;
}
