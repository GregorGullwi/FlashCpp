struct Record {
	int value;
};

template <class Type>
struct Owner {
	Type run(Type value) const & {
		return value;
	}

	private:
	double run(double value) const & {
		return value;
	}
};

struct ConstReferenceSelection {
	char marker[2];
};

struct RvalueReferenceSelection {
	char marker[3];
};

using ScalarMember = int (Owner<int>::*)(int) const &;
using RecordMember = Record (Owner<Record>::*)(Record) const &;

ConstReferenceSelection chooseConstReference(const ScalarMember&) {
	return {};
}

ConstReferenceSelection chooseConstReference(const RecordMember&) {
	return {};
}

RvalueReferenceSelection chooseRvalueReference(ScalarMember&&) {
	return {};
}

RvalueReferenceSelection chooseRvalueReference(RecordMember&&) {
	return {};
}

struct LvalueReferenceSelection {
	char marker[1];
};

LvalueReferenceSelection chooseReferenceRank(ScalarMember&) {
	return {};
}

ConstReferenceSelection chooseReferenceRank(const ScalarMember&) {
	return {};
}

RvalueReferenceSelection chooseReferenceRank(ScalarMember&&) {
	return {};
}

LvalueReferenceSelection chooseReferenceRank(RecordMember&) {
	return {};
}

ConstReferenceSelection chooseReferenceRank(const RecordMember&) {
	return {};
}

RvalueReferenceSelection chooseReferenceRank(RecordMember&&) {
	return {};
}

template <class Type>
auto selectConstReference() {
	return chooseConstReference(&Owner<Type>::run);
}

template <class Type>
auto selectRvalueReference() {
	return chooseRvalueReference(&Owner<Type>::run);
}

template <class Type>
auto selectReferenceRank() {
	return chooseReferenceRank(&Owner<Type>::run);
}

static_assert(sizeof(decltype(selectConstReference<int>())) ==
	sizeof(ConstReferenceSelection));
static_assert(sizeof(decltype(selectConstReference<Record>())) ==
	sizeof(ConstReferenceSelection));
static_assert(sizeof(decltype(selectRvalueReference<int>())) ==
	sizeof(RvalueReferenceSelection));
static_assert(sizeof(decltype(selectRvalueReference<Record>())) ==
	sizeof(RvalueReferenceSelection));
static_assert(sizeof(decltype(selectReferenceRank<int>())) ==
	sizeof(RvalueReferenceSelection));
static_assert(sizeof(decltype(selectReferenceRank<Record>())) ==
	sizeof(RvalueReferenceSelection));

int main() {
	return 0;
}
