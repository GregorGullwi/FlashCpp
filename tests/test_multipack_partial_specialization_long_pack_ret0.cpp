struct SmallRecord {
	char value;
};

struct WideRecord {
	long long value;
};

template <typename A, typename B>
struct Pair {
	A first;
	B second;
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

int main() {
	Box<
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>,
		Pair<char, int>, Pair<SmallRecord, long long>,
		Pair<double, WideRecord>, Pair<short, unsigned long long>
	> many{};

	return many.first_count == 256 && many.second_count == 256 && many.items.count == 256 ? 0 : 1;
}
