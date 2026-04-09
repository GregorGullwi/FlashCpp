struct E {};
struct A : E {};

struct B : E {
	[[no_unique_address]] A a;
};

static_assert(sizeof(B) >= 2);

int main() {
	return sizeof(B) >= 2 ? 0 : 1;
}
