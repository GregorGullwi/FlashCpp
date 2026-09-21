// A qualified namespace-scope interleaved pointer-to-array is still subject to
// the deferred conversion families: passing it to a void* parameter must fail
// closed as an ordinary no-match, not resolve through a flat projection.
namespace n {
int (*(*value)[3])[4] = nullptr;
}

int consume(void*);

int main() {
	consume(n::value);
	return 0;
}
