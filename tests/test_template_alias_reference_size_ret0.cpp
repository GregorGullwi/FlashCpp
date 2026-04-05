template<typename T>
struct AliasCarrier {
	using difference_type = long;

	static difference_type addTwice(difference_type value) {
		difference_type copy = value;
		difference_type& ref = copy;
		ref = ref + 2;
		return ref;
	}
};

int main() {
	long start = 40;
	return AliasCarrier<int>::addTwice(start) - 42;
}
