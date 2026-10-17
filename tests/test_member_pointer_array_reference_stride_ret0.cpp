struct PointerArray {
	int* elements[2];
	char* characters[2];
	double* reals[2];
	int* matrix[2][2];
};

void replace(int*& target, int* value) { target = value; }
void replace(char*& target, char* value) { target = value; }
void replace(double*& target, double* value) { target = value; }

int main() {
	int first = 1;
	int second = 2;
	int third = 3;
	char first_character = 'a';
	char second_character = 'b';
	char third_character = 'c';
	double first_real = 1.5;
	double second_real = 2.5;
	double third_real = 3.5;
	PointerArray pointers{
		{&first, &second},
		{&first_character, &second_character},
		{&first_real, &second_real},
		{{&first, &second}, {&second, &first}}};
	replace(pointers.elements[1], &third);
	replace(pointers.characters[1], &third_character);
	replace(pointers.reals[1], &third_real);
	replace(pointers.matrix[1][1], &third);
	return pointers.elements[0] == &first && pointers.elements[1] == &third &&
		pointers.characters[0] == &first_character && pointers.characters[1] == &third_character &&
		pointers.reals[0] == &first_real && pointers.reals[1] == &third_real &&
		pointers.matrix[1][0] == &second && pointers.matrix[1][1] == &third ? 0 : 1;
}
