struct ScalarSelection {
	char marker[1];
	static constexpr int value = 1;
};

struct Record {
	int value;
};

struct RecordSelection {
	char marker[2];
	static constexpr int value = 2;
};

template <class Type>
struct Owner {
	public:
	Type run(Type value) const & {
		return value;
	}

	private:
	double run(double value) const & {
		return value;
	}
};

ScalarSelection choose(int (Owner<int>::*)(int) const &) {
	return {};
}

RecordSelection choose(Record (Owner<Record>::*)(Record) const &) {
	return {};
}

template <class Type>
auto selectMemberFunctionAddress() {
	return choose(&Owner<Type>::run);
}

template <int Depth>
auto selectMemberFunctionAddressDeep() {
	if constexpr (Depth == 0) {
		return selectMemberFunctionAddress<int>();
	} else {
		return selectMemberFunctionAddressDeep<Depth - 1>();
	}
}

static_assert(sizeof(decltype(selectMemberFunctionAddress<int>())) ==
	sizeof(ScalarSelection));
static_assert(sizeof(decltype(selectMemberFunctionAddress<Record>())) ==
	sizeof(RecordSelection));
static_assert(sizeof(decltype(selectMemberFunctionAddressDeep<16>())) ==
	sizeof(ScalarSelection));

int main() {
	return 0;
}
