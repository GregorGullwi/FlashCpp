// A malformed array suffix inside a direct type-trait abstract declarator
// reports the array-bound diagnostic and its opening-bracket note.
int main() {
	return __is_pointer(int (*)[3 4]);
}
