struct Record {
	int value;
};

template <class Type>
struct Owner {
	template <class Argument>
	Type run(Argument) const &;
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
struct MemberFunctionTemplateAddressChain {
	static auto select() {
		return MemberFunctionTemplateAddressChain<Type, Depth - 1>::select();
	}
};

template <class Type>
struct MemberFunctionTemplateAddressChain<Type, 0> {
	static auto select() {
		return choose(&Owner<Type>::template run);
	}
};

static_assert(sizeof(decltype(MemberFunctionTemplateAddressChain<int, 16>::select())) ==
	sizeof(ScalarSelection));
static_assert(sizeof(decltype(MemberFunctionTemplateAddressChain<Record, 16>::select())) ==
	sizeof(RecordSelection));

int main() {
	return 0;
}
