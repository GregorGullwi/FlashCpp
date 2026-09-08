// Primary class-template type parameters publish TemplateDeclId + index so
// canonical TemplateParameter identity can distinguish parameters across
// templates without using spelling. Mixed widths exercise substitution.
template <typename T, typename U>
struct WidePair {
	T left;
	U right;
};

template <typename T>
struct NarrowBox {
	T value;
};

template <typename T>
T pass(T value) {
	return value;
}

int main() {
	WidePair<double, float> wide{4.5, 1.5f};
	NarrowBox<short> narrow{7};
	char tiny = 2;
	return static_cast<int>(pass(wide.left)) +
		static_cast<int>(pass(wide.right)) +
		static_cast<int>(pass(narrow.value)) +
		static_cast<int>(pass(tiny)) -
		(4 + 1 + 7 + 2);
}
