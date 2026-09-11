// Primary member class templates under published non-template enclosing
// classes publish TemplateDeclId (class-owned OwnerId + simple name) so
// type-parameter stamps and Spec lookup can distinguish Outer::Box from a
// namespace Box without using spelling as TypeId identity. Mixed widths and a
// struct exercise the published path.
struct Outer {
	template <typename T>
	struct Box {
		T value;
	};

	template <typename T, typename U>
	struct Pair {
		T left;
		U right;
	};
};

int useBox(Outer::Box<short> boxed) {
	return static_cast<int>(boxed.value);
}

int usePair(Outer::Pair<double, float> pair) {
	return static_cast<int>(pair.left) + static_cast<int>(pair.right);
}

int main() {
	Outer::Box<char> narrow{'A'};
	Outer::Pair<int, short> mixed{3, 4};
	return useBox(Outer::Box<short>{7}) +
		usePair(Outer::Pair<double, float>{2.5, 1.5f}) +
		static_cast<int>(narrow.value) +
		static_cast<int>(mixed.left) +
		static_cast<int>(mixed.right) -
		(7 + 2 + 1 + 'A' + 3 + 4);
}
