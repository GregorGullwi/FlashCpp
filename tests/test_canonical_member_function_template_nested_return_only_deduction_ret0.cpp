// A nested class-template return type supplies a member-function template's
// return-only parameter when resolving its address against a target signature.
struct Record {
	int value;
};

template <class Type>
struct ResultWrapper {
	Type value;
};

struct Factory {
	template <class Result>
	ResultWrapper<Result> make(int value) const & {
		return ResultWrapper<Result>{Result{value}};
	}
};

struct ScalarSelection {
	char marker[1];
};

struct RecordSelection {
	char marker[2];
};

ScalarSelection choose_int(ResultWrapper<int> (Factory::*)(int) const &) {
	return {};
}

RecordSelection choose_record(ResultWrapper<Record> (Factory::*)(int) const &) {
	return {};
}

auto select_int() {
	return choose_int(&Factory::make);
}

auto select_record() {
	return choose_record(&Factory::make);
}

static_assert(sizeof(decltype(select_int())) == sizeof(ScalarSelection));
static_assert(sizeof(decltype(select_record())) == sizeof(RecordSelection));

int main() {
	const ScalarSelection scalar = select_int();
	const RecordSelection record = select_record();
	return sizeof(scalar) == 1 && sizeof(record) == 2 ? 0 : 1;
}
