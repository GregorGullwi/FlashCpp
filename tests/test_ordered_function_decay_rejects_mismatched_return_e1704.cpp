// Function-pointer conversion must compare the complete callable type,
// including an ordered pointer/array return declarator.
int (*(*make_value(int scale))[2])[3];

void consume(int (*callback)(int));

int main() {
	consume(make_value);
	return 42;
}
