int calls = 0;

bool yes() {
	++calls;
	return true;
}

bool no() {
	++calls;
	return false;
}

int main() {
	if (false && yes()) return 1;
	if (calls != 0) return 2;
	if (!(true || yes())) return 3;
	if (calls != 0) return 4;

	if (!(true && yes())) return 5;
	if (calls != 1) return 6;
	if (!(false || yes())) return 7;
	if (calls != 2) return 8;

	if (false && no()) return 9;
	if (calls != 2) return 10;
	if (!(true || no())) return 11;
	if (calls != 2) return 12;
	return 0;
}
