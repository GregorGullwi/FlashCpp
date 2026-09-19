template <class First, class Second>
struct Both { First first; Second second; };

template <class Owner>
struct Captures {
	template <class Value>
	using Pointer = Owner*;
	template <class Value>
	using Mixed = Both<Owner, Value>;
};

using P = typename Captures<int>::template Pointer<char>;
using Q = typename Captures<short>::template Mixed<long>;

int main() {
	int value = 0;
	P pointer = &value;
	Q mixed{};
	*pointer = 41;
	mixed.first = 1;
	mixed.second = value;
	return mixed.first + mixed.second;
}
