struct ShapeMember {
	template <typename T = ShapeMember>
	int call(long) {
		return T::missing;
	}

	template <typename T = int>
	int call(int) {
		return 24;
	}
};

int main() {
	ShapeMember object;
	return object.call(0);
}
