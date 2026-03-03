struct Add {
	constexpr Add(int) {}

	constexpr int operator()(int lhs, int rhs) const {
		return lhs + rhs;
	}
};

constexpr Add add(0);
constinit int result = add(40, 2);

int main() {
	return result;
}
