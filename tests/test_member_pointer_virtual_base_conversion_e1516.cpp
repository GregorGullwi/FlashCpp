// A member-object pointer cannot convert from a virtual base owner to its
// derived class owner.
struct V {
	int value;
};

struct D : virtual V {};

int main() {
	int V::* base_member = &V::value;
	int D::* derived_member = base_member;
	return derived_member == nullptr;
}
