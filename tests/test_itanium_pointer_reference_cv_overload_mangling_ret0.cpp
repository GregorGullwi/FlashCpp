int select(int*& value) { return value != nullptr ? 11 : 1; }
int select(int* const& value) { return value != nullptr ? 12 : 2; }
int select(char*& value) { return value != nullptr ? 21 : 3; }
int select(char* const& value) { return value != nullptr ? 22 : 4; }
int select(double*& value) { return value != nullptr ? 31 : 5; }
int select(double* const& value) { return value != nullptr ? 32 : 6; }
int select(int**& value) { return value != nullptr ? 41 : 7; }
int select(int** const& value) { return value != nullptr ? 42 : 8; }
int select(int* const*& value) { return value != nullptr ? 43 : 9; }

int main() {
	int integer = 7;
	char character = 'a';
	double real = 3.5;
	int* integer_pointer = &integer;
	int* const const_integer_pointer = &integer;
	char* character_pointer = &character;
	char* const const_character_pointer = &character;
	double* real_pointer = &real;
	double* const const_real_pointer = &real;
	int** pointer_pointer = &integer_pointer;
	int** const const_pointer_pointer = &integer_pointer;
	int* const* pointer_to_const_pointer = &const_integer_pointer;
	const int selected = select(integer_pointer) + select(const_integer_pointer) +
		select(character_pointer) + select(const_character_pointer) +
		select(real_pointer) + select(const_real_pointer) + select(pointer_pointer) +
		select(const_pointer_pointer) + select(pointer_to_const_pointer);
	return selected == 255 ? 0 : 1;
}
