// Assignment to a static data member reaches the global symbol and the stored
// ordered pointer value round-trips: a local ordered pointer assigned into the
// member and read back converts to the original address. The pointer-assignment
// fast path used to overwrite the loaded temp and drop the store for qualified
// pointer lvalues, both for ordered members and ordinary pointer members.
struct Holder {
	static inline int (*(*int_value)[3])[4] = nullptr;
	static inline double (*(*double_value)[2])[3] = nullptr;
	static inline int* plain_value = nullptr;
};

int storage = 0;
void* int_captured = nullptr;
void* double_captured = nullptr;

int main() {
	int (*(*int_local)[3])[4] =
		reinterpret_cast<int (*(*)[3])[4]>(&storage);
	double (*(*double_local)[2])[3] =
		reinterpret_cast<double (*(*)[2])[3]>(&storage);
	Holder::int_value = int_local;
	Holder::double_value = double_local;
	Holder::plain_value = &storage;
	int_captured = Holder::int_value;
	double_captured = Holder::double_value;
	if (int_captured != static_cast<void*>(&storage)) {
		return 1;
	}
	if (double_captured != static_cast<void*>(&storage)) {
		return 2;
	}
	if (Holder::plain_value != &storage) {
		return 3;
	}
	return 42;
}
