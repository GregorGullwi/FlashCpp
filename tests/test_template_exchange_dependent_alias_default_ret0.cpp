template<typename Type>
Type exchange_value(Type& value, Type new_value) {
	Type old_value = value;
	value = new_value;
	return old_value;
}

template<typename Range>
using difference_t = int;

template<typename First, typename Second>
using select_first_t = First;

template<typename Range>
struct CachedPosition {
	difference_t<Range> run() {
		difference_t<Range> value = 7;
		return exchange_value(value, difference_t<Range>{-1});
	}
};

template<typename Left, typename FirstTail, typename SecondTail>
struct DualAliasUse {
	select_first_t<Left, FirstTail> take_first() {
		select_first_t<Left, FirstTail> value = 5;
		return exchange_value(value, select_first_t<Left, FirstTail>{6});
	}

	select_first_t<Left, SecondTail> take_second() {
		select_first_t<Left, SecondTail> value = 3;
		return exchange_value(value, select_first_t<Left, SecondTail>{4});
	}
};

struct Input {};
struct Output {};

int main() {
	CachedPosition<Input> position;
	if (position.run() != 7) {
		return 1;
	}
	DualAliasUse<int, Input, Output> dual;
	int first = dual.take_first();
	int second = dual.take_second();
	if (first != 5) {
		return 2;
	}
	if (second != 3) {
		return 3;
	}
	return 0;
}
