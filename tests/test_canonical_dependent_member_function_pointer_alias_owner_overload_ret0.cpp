template <class Type>
struct Carrier {};

struct IntTag {};

struct CharTag {};

template <class Type>
using ReadPointer = int (Carrier<Type>::*)() const;

static_assert(__is_same(ReadPointer<IntTag>, int (Carrier<IntTag>::*)() const));
static_assert(__is_same(ReadPointer<CharTag>, int (Carrier<CharTag>::*)() const));

void choose(int (Carrier<IntTag>::*)() const, int& selection) {
	selection = 1;
}

void choose(int (Carrier<CharTag>::*)() const, int& selection) {
	selection = 2;
}

int main() {
	ReadPointer<IntTag> int_member = nullptr;
	ReadPointer<CharTag> char_member = nullptr;
	int int_selection = 0;
	int char_selection = 0;
	choose(int_member, int_selection);
	choose(char_member, char_selection);
	return int_selection == 1 && char_selection == 2 ? 0 : 1;
}
