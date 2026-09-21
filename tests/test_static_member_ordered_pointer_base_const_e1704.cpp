// The static-member ordered type recovery must not bypass the conversion
// rules: adding const to the innermost base through a non-const chain is still
// ill-formed and fails closed.
struct Holder {
	static inline int (*(*value)[3])[4] = nullptr;
};

int consume(const int (*(*)[3])[4]);

int main() {
	consume(Holder::value);
	return 0;
}
