template<class T>
struct Outer {
	struct Inner {
		T prefix;
		int value;

		template<class U>
		operator U&() {
			return value;
		}
	};
};

int main() {
	Outer<int>::Inner narrow{3, 5};
	Outer<long long>::Inner wide{7, 11};
	int& narrow_reference = narrow;
	int& wide_reference = wide;
	narrow_reference = 17;
	wide_reference = 19;
	return narrow.value == 17 && wide.value == 19 ? 0 : 1;
}
