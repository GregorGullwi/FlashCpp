struct E {};

struct A : E {};
struct B : E {};
struct C : A, B {};

static_assert(sizeof(C) >= 2);

int main() {
	return sizeof(C) >= 2 ? 0 : 1;
}
