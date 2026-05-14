struct NoMember {
};

template <typename T = NoMember>
int shape_selected(long) {
	return T::missing;
}

template <typename T = int>
int shape_selected(int) {
	return 42;
}

int main() {
	return shape_selected(0);
}
