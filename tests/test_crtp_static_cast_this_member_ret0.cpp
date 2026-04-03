// Regression test: pointer static_cast from `this` inside a CRTP base must
// preserve the target semantic type so member access resolves on the derived.

template <typename Derived>
struct Base {
	bool engaged() const {
		return static_cast<const Derived*>(this)->flag;
	}
};

struct Holder : Base<Holder> {
	bool flag;
};

int main() {
	Holder h;
	h.flag = true;
	return h.engaged() ? 0 : 1;
}
