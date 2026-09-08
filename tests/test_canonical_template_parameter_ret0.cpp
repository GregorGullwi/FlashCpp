// Opaque TemplateParameter TypeIds distinguish TemplateDeclId + index.
// Spelling alone is not canonical identity; published decl keys are.
template <typename T, typename U>
struct Pair {
	T first;
	U second;
};

template <typename T>
T identity(T value) {
	return value;
}

int main() {
	Pair<int, short> pair{3, 5};
	return identity(pair.first) + identity(pair.second) - 8;
}
