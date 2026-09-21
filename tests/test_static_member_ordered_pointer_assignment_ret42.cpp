// Assignment to a static data member reaches the global symbol. The
// pointer-assignment fast path used to overwrite the loaded temp and drop the
// store for qualified pointer lvalues, both for ordered members and ordinary
// pointer members.
struct Holder {
	static inline int (*(*int_value)[3])[4] = nullptr;
	static inline double (*(*double_value)[2])[3] = nullptr;
	static inline int* plain_value = nullptr;
};

int storage = 0;

int main() {
	Holder::int_value = reinterpret_cast<int (*(*)[3])[4]>(&storage);
	Holder::double_value = reinterpret_cast<double (*(*)[2])[3]>(&storage);
	Holder::plain_value = &storage;
	if (Holder::int_value == nullptr) {
		return 1;
	}
	if (Holder::double_value == nullptr) {
		return 2;
	}
	if (Holder::plain_value != &storage) {
		return 3;
	}
	return 42;
}
