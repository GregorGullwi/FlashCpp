// A qualified ordered object pointer also reaches cv void*: argument typing
// carries the ordered spine and the conversion consumes it.
namespace n {
int (*(*value)[3])[4] = nullptr;
}

int consume(void* p) { return p == nullptr ? 1 : 0; }
int consume_cvoid(const void* p) { return p == nullptr ? 1 : 0; }

int main() {
	if (consume(n::value) != 1) {
		return 1;
	}
	if (consume_cvoid(n::value) != 1) {
		return 2;
	}
	return 42;
}
