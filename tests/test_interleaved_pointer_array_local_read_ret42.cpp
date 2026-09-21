// Reading a local ordered pointer variable must yield a pointer-sized value
// with pointer depth, not the base type's size. The local identifier path used
// the flat pointer fields and truncated the value.
int storage = 0;
void* captured = nullptr;

int main() {
	int (*(*local)[3])[4] =
		reinterpret_cast<int (*(*)[3])[4]>(&storage);
	if (sizeof(local) != sizeof(void*)) {
		return 1;
	}
	captured = local;
	return captured == static_cast<void*>(&storage) ? 42 : 1;
}
