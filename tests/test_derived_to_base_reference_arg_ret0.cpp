struct Prefix {
	int tag;
};

struct Shape {
	int id;
};

struct Circle : Prefix, Shape {
	int radius;
};

int describe_ref(const Shape& shape) {
	return shape.id;
}

int main() {
	Circle circle;
	circle.tag = 11;
	circle.id = 37;
	circle.radius = 5;
	return describe_ref(circle) == 37 ? 0 : 1;
}
