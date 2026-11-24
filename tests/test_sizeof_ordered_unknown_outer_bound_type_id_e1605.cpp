// An array of unknown bound has no object size ([expr.sizeof]/2), including
// when the unknown bound is the outer wrapper of a pointer-to-row declarator.
int main() {
	return static_cast<int>(sizeof(int (*[])[4]));
}
