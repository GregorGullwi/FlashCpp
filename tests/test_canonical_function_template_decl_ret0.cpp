// Non-overloaded namespace free function templates publish TemplateDeclId so
// later type-parameter stamping can distinguish templates without using
// spelling as TypeId identity. Mixed widths and a struct exercise the path;
// overloaded sets stay unpublished until signature-aware keys land.
template <typename T>
T identityValue(T value) {
	return value;
}

template <typename T, typename U>
int mixValues(T left, U right) {
	return static_cast<int>(identityValue(left)) + static_cast<int>(identityValue(right));
}

struct Boxed {
	short tag;
};

int main() {
	Boxed boxed{4};
	char narrow = 1;
	return mixValues(3.5, 2.5f) + mixValues(boxed.tag, narrow) - (3 + 2 + 4 + 1);
}
