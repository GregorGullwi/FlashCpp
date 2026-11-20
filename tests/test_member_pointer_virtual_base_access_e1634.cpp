// Member-pointer access through a virtual base is rejected until the runtime
// adjustment is implemented.

struct V {
	int v;
};

struct D : virtual V {};

int main() {
	int V::* pointer = &V::v;
	D object;
	return object.*pointer;
}
