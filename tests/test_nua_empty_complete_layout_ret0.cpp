// Reduced: empty [[no_unique_address]] members must not make the enclosing
// aggregate look layout-incomplete for runtime value sizing (sizeof / ABI).

struct Empty {};

struct Holder {
	[[no_unique_address]] Empty e;
	int x;
};

int take_by_value(Holder h) {
	return h.x;
}

int main() {
	Holder h{};
	h.x = 7;
	return take_by_value(h) == 7 ? 0 : 1;
}
