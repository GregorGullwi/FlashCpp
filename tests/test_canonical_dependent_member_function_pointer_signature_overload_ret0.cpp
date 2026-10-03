struct ScalarSelection {
	static constexpr int value = 1;
};

struct Record {
	int value;
};

struct RecordSelection {
	static constexpr int value = 2;
};

template <class Type>
struct Owner {
	Type run(Type value) const & {
		return value;
	}

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
using RunPointer = Type (Owner<Type>::*)(Type) const &;

template <class Type>
struct SelectionFor;

template <>
struct SelectionFor<int> {
	using type = ScalarSelection;
};

template <>
struct SelectionFor<Record> {
	using type = RecordSelection;
};

template <class Type>
int selectMemberFunctionAddress() {
	RunPointer<Type> member = &Owner<Type>::run;
	typename SelectionFor<Type>::type selection = choose(member);
	return selection.value;
}

int main() {
	return selectMemberFunctionAddress<int>() == 1 &&
		selectMemberFunctionAddress<Record>() == 2
		? 0
		: 1;
}
