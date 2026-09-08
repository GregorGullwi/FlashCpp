// Opaque TemplateParameter TypeIds distinguish TemplateDeclId + index across
// mixed native widths and a struct; spelling alone is not canonical identity.
template <typename T, typename U>
struct Pair {
	T first;
	U second;
};

template <typename T>
struct Box {
	T value;
};

template <typename T>
T identity(T value) {
	return value;
}

template <typename T, typename U>
int mix(T left, U right) {
	return static_cast<int>(identity(left)) + static_cast<int>(identity(right));
}

int main() {
	Pair<double, float> floating{3.5, 2.5f};
	Box<short> boxed{4};
	char narrow = 1;
	return mix(floating.first, floating.second) +
		mix(boxed.value, narrow) -
		(3 + 2 + 4 + 1);
}
