// Taking the address of a shadowed member array must preserve the object's
// local declaration identity through address computation.
struct Holder {
	int values[2][2];
};

int main() {
	Holder owner;
	owner.values[0][0] = 1;
	owner.values[0][1] = 2;
	owner.values[1][0] = 3;
	owner.values[1][1] = 4;
	int (*outer)[2][2] = &owner.values;
	{
		Holder owner;
		owner.values[0][0] = 5;
		owner.values[0][1] = 6;
		owner.values[1][0] = 7;
		owner.values[1][1] = 8;
		int (*inner)[2][2] = &owner.values;
		if ((*inner)[1][1] != 8) return 1;
		if ((*outer)[0][0] != 1) return 2;
	}
	if ((*outer)[1][1] != 4) return 3;
	return 0;
}
