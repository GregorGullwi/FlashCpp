// Pointer elements whose pointee is a record still occupy one machine word in
// a two-dimensional array. The distinct long long payloads make a row-sized or
// pointee-sized stride read a wrong pointer or the wrong field.
struct Wide {
	long long low;
	long long high;
};

Wide w00 = {1LL << 33, (1LL << 35) + 1};
Wide w01 = {1LL << 37, (1LL << 39) + 3};
Wide w02 = {1LL << 41, (1LL << 43) + 5};
Wide w10 = {1LL << 45, (1LL << 47) + 7};
Wide w11 = {1LL << 49, (1LL << 51) + 9};
Wide w12 = {1LL << 53, (1LL << 55) + 11};

Wide* cells[2][3] = {{&w00, &w01, &w02}, {&w10, &w11, &w12}};

int main() {
	Wide* (*p)[2][3] = &cells;
	if ((*p)[0][0] != &w00 || (*p)[0][1] != &w01 || (*p)[0][2] != &w02) return 1;
	if ((*p)[1][0] != &w10 || (*p)[1][1] != &w11 || (*p)[1][2] != &w12) return 2;
	if ((*p)[0][1]->high != (1LL << 39) + 3) return 3;
	if ((*p)[1][2]->low != 1LL << 53) return 4;
	if ((*p)[1][0]->high != (1LL << 47) + 7) return 5;

	(*p)[1][1]->low = 99;
	if (w11.low != 99) return 6;
	if ((*p)[1][1]->low != 99) return 7;
	// The next element still points at its own record.
	if ((*p)[1][2] != &w12 || (*p)[1][2]->low != 1LL << 53) return 8;

	return 0;
}
