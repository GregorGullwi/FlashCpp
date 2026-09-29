// Member-function-pointer owners spelled as class-template specializations
// (Holder<int>::*) must keep distinct TypeId identity for overload ranking.
template <class Type>
struct Holder {
	int run();
};

struct IntOwnerSelection {
	char marker[1];
};

struct CharOwnerSelection {
	char marker[2];
};

IntOwnerSelection chooseRun(int (Holder<int>::*)());
CharOwnerSelection chooseRun(int (Holder<char>::*)());

static_assert(sizeof(decltype(chooseRun(&Holder<int>::run))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseRun(&Holder<char>::run))) ==
	sizeof(CharOwnerSelection));
static_assert(sizeof(decltype(chooseRun(
	static_cast<int (Holder<int>::*)()>(nullptr)))) ==
	sizeof(IntOwnerSelection));
static_assert(sizeof(decltype(chooseRun(
	static_cast<int (Holder<char>::*)()>(nullptr)))) ==
	sizeof(CharOwnerSelection));

int main() {
	return sizeof(decltype(chooseRun(&Holder<int>::run))) ==
				   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseRun(&Holder<char>::run))) ==
					   sizeof(CharOwnerSelection) &&
				   sizeof(decltype(chooseRun(
					   static_cast<int (Holder<int>::*)()>(nullptr)))) ==
					   sizeof(IntOwnerSelection) &&
				   sizeof(decltype(chooseRun(
					   static_cast<int (Holder<char>::*)()>(nullptr)))) ==
					   sizeof(CharOwnerSelection)
			   ? 0
			   : 1;
}
