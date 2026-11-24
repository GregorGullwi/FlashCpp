// Publishing a pointer-to-array member's complete pointee must not bypass the
// receiver check: only a class object (or a pointer to one for ->*) can be the
// left operand of .* or ->*.

struct S {
	int (*row)[3];
};

int main() {
	int value = 0;
	int (&row)[3] = value.*(&S::row);
	return row[0];
}
