struct Record {
	int value;
};

template<class T, class U>
struct Pair {
	using type = T;
};

template<class T, class U>
using First = typename Pair<T, U>::type;

static_assert(__is_same(Pair<Record*, long>::type, Record*));
static_assert(__is_same(First<Record*, long>, Record*));
static_assert(__is_same(First<const Record&, long>, const Record&));

int main() {
	Record record{7};
	First<Record*, long> pointer = &record;
	First<const Record&, long> reference = record;
	if (pointer->value != 7) return 1;
	if (reference.value != 7) return 2;
	return 0;
}
