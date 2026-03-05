namespace math {
	struct Vector {
		int x, y;
		int sum() const { return x + y; }
	};
	int compute(Vector v) { return v.sum(); }
}
namespace physics {
	struct Vector {
		int magnitude, direction;
		int mag() const { return magnitude; }
	};
	int compute(Vector v) { return v.mag(); }
}
int main() {
	math::Vector mv{10, 20};
	physics::Vector pv{12, 0};
	return math::compute(mv) + physics::compute(pv); // Expected: 42
}
