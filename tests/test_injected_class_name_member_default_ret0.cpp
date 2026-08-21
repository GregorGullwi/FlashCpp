// C++20 [temp.local]: inside a class template, the injected-class-name
// denotes the current specialization. Member-template defaults such as
// `template<class Myself = Pair>` must therefore bind Myself to Pair<A, B>,
// not the primary template, so `typename Myself::first_type` is usable.

template <class First, class Second>
struct Pair {
	using first_type = First;
	using second_type = Second;

	First first;
	Second second;

	template <class Myself = Pair,
		int FirstSize = sizeof(typename Myself::first_type)>
	int first_size() {
		return FirstSize;
	}
};

struct Tiny {
	char byte;
};

struct Wide {
	int a;
	int b;
};

int main() {
	Pair<char, Tiny> tiny_pair{};
	Pair<int, Wide> wide_pair{};
	tiny_pair.first = 3;
	wide_pair.first = 7;
	return (tiny_pair.first_size() == 1 &&
			wide_pair.first_size() == sizeof(int) &&
			tiny_pair.first == 3 &&
			wide_pair.first == 7)
		? 0
		: 1;
}
