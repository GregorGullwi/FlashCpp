struct Record {
	int value;
};

template<class Type>
struct ResultWrapper {
	Type value;
};

template<class Owner>
struct Factory {
	template<class Result>
	ResultWrapper<Result> make();
};

struct IntFactorySelection {};
struct RecordFactorySelection {};

IntFactorySelection choose(ResultWrapper<int> (Factory<int>::*)()) {
	return {};
}

RecordFactorySelection choose(ResultWrapper<Record> (Factory<Record>::*)()) {
	return {};
}

template<class Owner>
auto select_factory() {
	return choose(&Factory<Owner>::template make);
}

auto select_record_directly() {
	return choose(&Factory<Record>::template make);
}

static_assert(__is_same(
	decltype(select_factory<int>()),
	IntFactorySelection));
static_assert(__is_same(
	decltype(select_factory<Record>()),
	RecordFactorySelection));
static_assert(__is_same(
	decltype(select_record_directly()),
	RecordFactorySelection));

int main() {
	return 0;
}
