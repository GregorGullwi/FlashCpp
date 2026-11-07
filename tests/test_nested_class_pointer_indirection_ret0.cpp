template<class T>
struct Outer {
	struct Inner { T value; };
	Inner object;
	Inner* item;
	T read() { return item->value; }
	T readObject() { return object.value; }
	T readShadow() {
		Inner* item = nullptr;
		return item ? item->value : T{};
	}
};

int main() {
	Outer<int>::Inner inner{7};
	if (inner.value != 7) return 5;
	Outer<int>::Inner* outside = &inner;
	if (outside->value != 7) return 6;
	Outer<int> outer{};
	outer.object.value = 9;
	if (outer.readObject() != 9) return 8;
	outer.item = &inner;
	if (outer.read() != 7) return 2;
	if (outer.readShadow() != 0) return 9;
	Outer<long long>::Inner wide{11};
	Outer<long long> wide_outer{};
	wide_outer.item = &wide;
	if (wide_outer.read() != 11) return 3;
	if (outer.read() != 7) return 4;
	return 0;
}
