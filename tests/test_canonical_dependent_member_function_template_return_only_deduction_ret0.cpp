struct Record {
	int value;
};

template <class Type>
struct Owner {
	template <class Result>
	Result run(int) const & {
		return {};
	}
};

struct ScalarSelection {
	char marker[1];
};

struct RecordSelection {
	char marker[2];
};

ScalarSelection choose(int (Owner<int>::*)(int) const &) {
	return {};
}

RecordSelection choose(Record (Owner<Record>::*)(int) const &) {
	return {};
}

template <class Type, int Depth>
struct ReturnOnlyMemberFunctionTemplateAddressChain {
	static auto select() {
		return ReturnOnlyMemberFunctionTemplateAddressChain<Type, Depth - 1>::select();
	}
};

template <class Type>
struct ReturnOnlyMemberFunctionTemplateAddressChain<Type, 0> {
	static auto select() {
		return choose(&Owner<Type>::template run);
	}
};

static_assert(sizeof(decltype(ReturnOnlyMemberFunctionTemplateAddressChain<int, 16>::select())) ==
	sizeof(ScalarSelection));
static_assert(sizeof(decltype(ReturnOnlyMemberFunctionTemplateAddressChain<Record, 16>::select())) ==
	sizeof(RecordSelection));

int main() {
	const ScalarSelection scalar =
		ReturnOnlyMemberFunctionTemplateAddressChain<int, 16>::select();
	const RecordSelection record =
		ReturnOnlyMemberFunctionTemplateAddressChain<Record, 16>::select();
	return sizeof(scalar) == 1 && sizeof(record) == 2 ? 0 : 1;
}
