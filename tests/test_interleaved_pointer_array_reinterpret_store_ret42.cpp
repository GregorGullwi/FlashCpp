// A reinterpret_cast to an ordered pointer type used directly as an assignment
// right-hand side must carry pointer size and depth. The cast lowering used the
// target type's flat fields, so the stored value was base-sized.
int (*(*global_value)[3])[4] = nullptr;

struct Holder {
	static inline int (*(*value)[3])[4] = nullptr;
};

int storage = 0;
void* captured = nullptr;

int main() {
	global_value = reinterpret_cast<int (*(*)[3])[4]>(&storage);
	captured = global_value;
	if (captured != static_cast<void*>(&storage)) {
		return 1;
	}
	Holder::value = reinterpret_cast<int (*(*)[3])[4]>(&storage);
	captured = Holder::value;
	if (captured != static_cast<void*>(&storage)) {
		return 2;
	}
	return 42;
}
