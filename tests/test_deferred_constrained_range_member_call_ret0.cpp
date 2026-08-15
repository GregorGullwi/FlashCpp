template <class Type>
concept Copyable = requires(Type value) {
	Type(value);
};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

enum class RangeKind {
	Unsized,
	Sized
};

template <class Iterator, class Sentinel, RangeKind Kind>
	requires (Kind == RangeKind::Sized || !Copyable<Iterator>)
struct Range {
	Iterator first;
	Sentinel last;
	Range(Iterator firstValue, Sentinel lastValue)
		: first(firstValue), last(lastValue) {}

	Iterator begin() const
		requires Copyable<Iterator>
	{
		return first;
	}

	Sentinel end() const {
		return last;
	}
};

template <class Container>
auto rangeBegin(Container& container) -> decltype(container.begin()) {
	return container.begin();
}

template <class Container>
auto rangeEnd(const Container& container) -> decltype(container.end()) {
	return container.end();
}

int main() {
	struct Iterator {
		long long value;
	};
	Iterator first;
	first.value = 4;
	Range<Iterator, int, RangeKind::Sized> range(first, 7);
	static_assert(SameType<decltype(rangeBegin(range)), Iterator>::value);
	static_assert(SameType<decltype(rangeEnd(range)), int>::value);
	rangeBegin(range);
	rangeEnd(range);
	return 0;
}
