// C++20 [conv.array] and [conv.ptr]: the array lvalue decays to its first
// element, then the object pointer converts to void* for both initialization
// and an overload argument.
int consume(void* pointer) {
	return pointer != nullptr ? 1 : 0;
}

int main() {
	long anchor_storage = 0;
	long* anchor = &anchor_storage;
	int (*(*ordered)[3])[4] =
		reinterpret_cast<int (*(*)[3])[4]>(anchor);
	void* converted = *ordered;
	if (converted != static_cast<void*>(anchor)) {
		return 1;
	}
	return consume(*ordered) == 1 ? 42 : 2;
}
