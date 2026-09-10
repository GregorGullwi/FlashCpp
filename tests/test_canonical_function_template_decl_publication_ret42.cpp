namespace CanonicalFunctionTemplatePublication {
	template<typename T>
	T pass(T value) {
		return value;
	}
}

int main() {
	return CanonicalFunctionTemplatePublication::pass(42);
}
