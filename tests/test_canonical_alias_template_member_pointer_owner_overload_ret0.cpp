// Alias-template targets that are member pointers with class-template owners
// must keep distinct owner TypeId identity after substitution.
template <class Type>
struct Holder {
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

template <class Type>
using FieldPointer = int Holder<Type>::*;

template <class Type>
using RunPointer = int (Holder<Type>::*)();

static_assert(sizeof(decltype(chooseField(static_cast<FieldPointer<int>>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseField(static_cast<FieldPointer<char>>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<RunPointer<int>>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<RunPointer<char>>(nullptr)))) ==
	sizeof(CharOwnerSelection));

int main() {
	return sizeof(decltype(chooseField(static_cast<FieldPointer<int>>(nullptr)))) ==
				   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseField(static_cast<FieldPointer<char>>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<RunPointer<int>>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<RunPointer<char>>(nullptr)))) ==
					   sizeof(CharOwnerSelection)
			   ? 0
			   : 1;
}
