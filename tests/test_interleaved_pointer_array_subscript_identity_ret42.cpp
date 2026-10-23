// Built-in subscripting on an interleaved pointer/array object must step through
// the ordered declarator: an array of pointers followed by a pointer-to-array is
// not one contiguous row-major array. The flat projection flattened both array
// bounds, so p[0] used the base element size instead of the pointer stride.
int storage[4] = {7, 8, 9, 10};
void* raw[3];

int main() {
	raw[0] = &storage;
	raw[1] = &storage;
	raw[2] = &storage;
	int (*(*p)[3])[4] = reinterpret_cast<int (*(*)[3])[4]>(raw);
	if ((*p)[0] != &storage) return 1;
	if (p[0][0] != &storage) return 2;
	if (p[0][0][0][0] != 7) return 3;
	if ((*p)[0][0][0] != 7) return 4;
	return 42;
}
