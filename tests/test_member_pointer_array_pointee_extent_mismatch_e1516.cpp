// Publishing the member array's complete pointee keeps its exact extent, so a
// member pointer declared with a different array bound cannot be initialized
// from the address of that member.

struct C {
	int arr[3];
};

int main() {
	int (C::*pointer)[4] = &C::arr;
	return (pointer == nullptr) ? 0 : 1;
}
