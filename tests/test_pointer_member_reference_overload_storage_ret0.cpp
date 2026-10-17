int take(int*& value) { return value == nullptr ? 1 : 10; }
int take(int* const& value) { return value == nullptr ? 2 : 20; }
int take(char*& value) { return value == nullptr ? 3 : 30; }
int take(char* const& value) { return value == nullptr ? 4 : 40; }
int take(double*& value) { return value == nullptr ? 5 : 50; }
int take(double* const& value) { return value == nullptr ? 6 : 60; }
void replace(int*& value, int* replacement) { value = replacement; }

struct PointerMembers {
	int* member;
	int* elements[2];
	int* const declared_const;
	char* character;
	double* real;

	PointerMembers(int* value, char* character_value, double* real_value)
		: member(value), elements{value, value}, declared_const(value), character(character_value), real(real_value) {}
};

int main() {
	int value = 7;
	int replacement_value = 9;
	char character = 'a';
	double real = 3.5;
	int* replacement_pointer = &replacement_value;
	PointerMembers mutable_pointers(&value, &character, &real);
	const PointerMembers& const_pointers = mutable_pointers;
	const PointerMembers const_object(&value, &character, &real);
	int selected = take(mutable_pointers.member) + take(mutable_pointers.elements[0]) + take(mutable_pointers.elements[1]);
	selected += take(mutable_pointers.declared_const) + take(const_pointers.member) + take(const_pointers.elements[0]);
	selected += take(const_pointers.elements[1]) + take(const_pointers.declared_const);
	selected += take(const_object.member) + take(const_object.elements[1]) + take(const_object.declared_const);
	selected += take(const_pointers.character) + take(const_pointers.real);
	replace(mutable_pointers.member, replacement_pointer);
	return selected == 290 && mutable_pointers.member == replacement_pointer ? 0 : 1;
}
