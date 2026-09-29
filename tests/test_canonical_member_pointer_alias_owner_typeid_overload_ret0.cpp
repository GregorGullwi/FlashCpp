// Member-pointer using-aliases that share a flat type_index must still rank by
// owner TypeId when projected through static_cast.
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

using IntFieldPointer = int Holder<int>::*;
using CharFieldPointer = int Holder<char>::*;
using IntRunPointer = int (Holder<int>::*)();
using CharRunPointer = int (Holder<char>::*)();

static_assert(sizeof(decltype(chooseField(static_cast<IntFieldPointer>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseField(static_cast<CharFieldPointer>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<IntRunPointer>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<CharRunPointer>(nullptr)))) ==
	sizeof(CharOwnerSelection));

int main() {
	return sizeof(decltype(chooseField(static_cast<IntFieldPointer>(nullptr)))) ==
				   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseField(static_cast<CharFieldPointer>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<IntRunPointer>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<CharRunPointer>(nullptr)))) ==
					   sizeof(CharOwnerSelection)
			   ? 0
			   : 1;
}
