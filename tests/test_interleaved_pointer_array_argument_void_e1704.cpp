// Passing an interleaved pointer-to-array object to a void* parameter is a
// deferred conversion family. It must fail closed as an ordinary no-match
// rather than abort at the flat projection guard.
int (*(*value)[3])[4] = nullptr;

int consume(void*);

int main() {
	consume(value);
	return 0;
}
