struct Target { int value; };

struct Source {
	explicit operator Target() const { return Target{7}; }
};

int main() {
	Target target = Source{};
	return target.value;
}
