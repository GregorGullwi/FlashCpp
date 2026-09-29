// Member-object-pointer owners spelled as class-template specializations
// (Holder<int>::*) must keep distinct TypeId identity for overload ranking.
template <class Type>
struct Holder {
	Type value;
};

struct IntFieldSelection {
	char marker[1];
};

struct CharFieldSelection {
	char marker[2];
};

IntFieldSelection chooseField(int Holder<int>::*);
CharFieldSelection chooseField(char Holder<char>::*);

using IntFieldPointer = int Holder<int>::*;
using CharFieldPointer = char Holder<char>::*;

static_assert(sizeof(decltype(chooseField(&Holder<int>::value))) ==
	sizeof(IntFieldSelection));
static_assert(sizeof(decltype(chooseField(&Holder<char>::value))) ==
	sizeof(CharFieldSelection));
static_assert(sizeof(decltype(chooseField(static_cast<IntFieldPointer>(nullptr)))) ==
	sizeof(IntFieldSelection));
static_assert(sizeof(decltype(chooseField(static_cast<CharFieldPointer>(nullptr)))) ==
	sizeof(CharFieldSelection));

int main() {
	return sizeof(decltype(chooseField(&Holder<int>::value))) ==
				   sizeof(IntFieldSelection) &&
				   sizeof(decltype(chooseField(&Holder<char>::value))) ==
					   sizeof(CharFieldSelection) &&
				   sizeof(decltype(chooseField(static_cast<IntFieldPointer>(nullptr)))) ==
					   sizeof(IntFieldSelection) &&
				   sizeof(decltype(chooseField(static_cast<CharFieldPointer>(nullptr)))) ==
					   sizeof(CharFieldSelection)
			   ? 0
			   : 1;
}
