// Class-scope typedef mop/MFP owners must publish distinct owner TypeId
// identity for cast overload ranking (plain and template-id owners).
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

IntOwnerSelection chooseField(int PlainInt::*);
CharOwnerSelection chooseField(int PlainChar::*);
IntOwnerSelection chooseHolderField(int Holder<int>::*);
CharOwnerSelection chooseHolderField(int Holder<char>::*);
IntOwnerSelection chooseRun(int (PlainInt::*)());
CharOwnerSelection chooseRun(int (PlainChar::*)());
IntOwnerSelection chooseHolderRun(int (Holder<int>::*)());
CharOwnerSelection chooseHolderRun(int (Holder<char>::*)());

struct Typedefs {
	typedef int PlainInt::* PlainIntField;
	typedef int PlainChar::* PlainCharField;
	typedef int Holder<int>::* HolderIntField;
	typedef int Holder<char>::* HolderCharField;
	typedef int (PlainInt::* PlainIntRun)();
	typedef int (PlainChar::* PlainCharRun)();
	typedef int (Holder<int>::* HolderIntRun)();
	typedef int (Holder<char>::* HolderCharRun)();
};

static_assert(sizeof(decltype(chooseField(static_cast<Typedefs::PlainIntField>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseField(static_cast<Typedefs::PlainCharField>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseHolderField(static_cast<Typedefs::HolderIntField>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseHolderField(static_cast<Typedefs::HolderCharField>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<Typedefs::PlainIntRun>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseRun(static_cast<Typedefs::PlainCharRun>(nullptr)))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseHolderRun(static_cast<Typedefs::HolderIntRun>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseHolderRun(static_cast<Typedefs::HolderCharRun>(nullptr)))) ==
	sizeof(CharOwnerSelection));

int main() {
	return sizeof(decltype(chooseField(static_cast<Typedefs::PlainIntField>(nullptr)))) ==
				   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseField(static_cast<Typedefs::PlainCharField>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseHolderField(static_cast<Typedefs::HolderIntField>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseHolderField(static_cast<Typedefs::HolderCharField>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<Typedefs::PlainIntRun>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseRun(static_cast<Typedefs::PlainCharRun>(nullptr)))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseHolderRun(static_cast<Typedefs::HolderIntRun>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseHolderRun(static_cast<Typedefs::HolderCharRun>(nullptr)))) ==
					   sizeof(CharOwnerSelection)
			   ? 0
			   : 1;
}
