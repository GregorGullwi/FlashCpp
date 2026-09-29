// Alias-template member-pointer targets must publish distinct owner TypeId
// identity after substitution for both class-template owners (Holder<Type>::*)
// and type-parameter owners (Type::*).
template <class Type>
struct Holder {
	int value;
	int run();
};

struct PlainInt {
	int value;
	int run();
};

struct PlainChar {
	int value;
	int run();
};

struct IntOwnerSelection {
	char marker[1];
};

struct CharOwnerSelection {
	char marker[2];
};

IntOwnerSelection chooseField(int Holder<int>::*);
CharOwnerSelection chooseField(int Holder<char>::*);
IntOwnerSelection chooseRun(int (Holder<int>::*)());
CharOwnerSelection chooseRun(int (Holder<char>::*)());
IntOwnerSelection choosePlainField(int PlainInt::*);
CharOwnerSelection choosePlainField(int PlainChar::*);
IntOwnerSelection choosePlainRun(int (PlainInt::*)());
CharOwnerSelection choosePlainRun(int (PlainChar::*)());

template <class Type>
using FieldPointer = int Holder<Type>::*;

template <class Type>
using RunPointer = int (Holder<Type>::*)();

template <class Type>
using DirectField = int Type::*;

template <class Type>
using DirectRun = int (Type::*)();

static_assert(sizeof(decltype(chooseField(static_cast<FieldPointer<int>>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseField(static_cast<FieldPointer<char>>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<RunPointer<int>>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<RunPointer<char>>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(choosePlainField(static_cast<DirectField<PlainInt>>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(choosePlainField(static_cast<DirectField<PlainChar>>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(choosePlainRun(static_cast<DirectRun<PlainInt>>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(choosePlainRun(static_cast<DirectRun<PlainChar>>(nullptr)))) ==
	sizeof(CharOwnerSelection));

int main() {
	return sizeof(decltype(chooseField(static_cast<FieldPointer<int>>(nullptr)))) ==
				   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseField(static_cast<FieldPointer<char>>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<RunPointer<int>>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<RunPointer<char>>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(choosePlainField(static_cast<DirectField<PlainInt>>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(choosePlainField(static_cast<DirectField<PlainChar>>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(choosePlainRun(static_cast<DirectRun<PlainInt>>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(choosePlainRun(static_cast<DirectRun<PlainChar>>(nullptr)))) ==
					   sizeof(CharOwnerSelection)
			   ? 0
			   : 1;
}
