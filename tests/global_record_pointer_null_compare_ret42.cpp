// A namespace-scope pointer to a record compared against nullptr is a built-in
// pointer comparison ([expr.eq]), not a struct spaceship rewrite. It must lower
// even though the global is absent from the codegen-side symbol table, unlike a
// local pointer. Cover == and != on small and large record pointers.
struct Small {
	short value;
};

struct Large {
	long first;
	short second;
};

Small* small = nullptr;
Large* large = nullptr;
Large* anchored = nullptr;

int main() {
	Large node{7, 8};
	anchored = &node;
	if (small != nullptr || large != nullptr) {
		return 1;
	}
	if (anchored == nullptr || !(anchored != nullptr)) {
		return 2;
	}
	return anchored->first + anchored->second == 15 ? 42 : 3;
}
