// Regression: instantiation-context bindings must preserve template-template
// parameter kind as TemplateParameterKind::Template.
// This exercises nested dependent lookup through a template-template argument.

template <typename T>
struct carrier {
	static constexpr int value = sizeof(T);
};

template <template <typename> class TT>
struct wrapper {
	struct nested {
		static constexpr int eval = TT<int>::value;
	};
};

int main() {
	return wrapper<carrier>::nested::eval - 4;
}
