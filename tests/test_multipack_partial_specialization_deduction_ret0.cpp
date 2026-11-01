template <typename A, typename B>
struct Pair {
	A first;
	B second;
};

struct SmallRecord {
	char value;
};

struct WideRecord {
	long long value;
};

template <typename... T>
struct List {
	int count = sizeof...(T);
};

template <typename... T>
struct Box;

template <typename... A, typename... B>
struct Box<Pair<A, B>...> {
	List<Pair<A, B>...> items;
	int first_count = sizeof...(A);
	int second_count = sizeof...(B);
};

using concreteFull = Box<Pair<char, int>, Pair<SmallRecord, long long>, Pair<double, WideRecord>>;
using concreteSingle = Box<Pair<char, int>>;
using concreteEmpty = Box<>;

template <typename... T>
struct EqualBox;

template <typename... T>
struct EqualBox<Pair<T, T>...> {};

template <typename... T>
struct ReuseBox;

template <typename T, typename... U>
struct ReuseBox<Pair<T, U>...> {};

int main() {
	Box<Pair<char, int>, Pair<SmallRecord, long long>, Pair<double, WideRecord>> mixed{};
	Box<Pair<char, int>> single{};
	Box<> empty{};
	if (mixed.first_count != 3) return 10 + mixed.first_count;
	if (mixed.second_count != 3) return 20 + mixed.second_count;
	if (mixed.items.count != 3) return 30 + mixed.items.count;
	if (single.first_count != 1 || single.second_count != 1 || single.items.count != 1) return 40;
	if (empty.first_count != 0 || empty.second_count != 0 || empty.items.count != 0) return 50;
	return 0;
}
