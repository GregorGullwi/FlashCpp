// Typedef member-function-pointer declarators must publish distinct owner
// TypeId identity for cast overload ranking (plain and template-id owners).
template <class Type>
struct Holder {
	int run();
};

struct PlainInt {
	int run();
};

struct PlainChar {
	int run();
};

struct IntOwnerSelection {
	char marker[1];
};

struct CharOwnerSelection {
	char marker[2];
};

IntOwnerSelection choosePlain(int (PlainInt::*)());
CharOwnerSelection choosePlain(int (PlainChar::*)());
IntOwnerSelection chooseHolder(int (Holder<int>::*)());
CharOwnerSelection chooseHolder(int (Holder<char>::*)());

typedef int (PlainInt::* PlainIntRun)();
typedef int (PlainChar::* PlainCharRun)();
typedef int (Holder<int>::* HolderIntRun)();
typedef int (Holder<char>::* HolderCharRun)();

static_assert(sizeof(decltype(choosePlain(static_cast<PlainIntRun>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(choosePlain(static_cast<PlainCharRun>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseHolder(static_cast<HolderIntRun>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseHolder(static_cast<HolderCharRun>(nullptr)))) ==
	sizeof(CharOwnerSelection));

int main() {
	return sizeof(decltype(choosePlain(static_cast<PlainIntRun>(nullptr)))) ==
				   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(choosePlain(static_cast<PlainCharRun>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseHolder(static_cast<HolderIntRun>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseHolder(static_cast<HolderCharRun>(nullptr)))) ==
					   sizeof(CharOwnerSelection)
			   ? 0
			   : 1;
}
