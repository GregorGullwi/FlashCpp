namespace MemberTemplateNamespaceExplicitNoArgs {
	struct Holder {
		template<typename T>
		static T value() noexcept {
			return static_cast<T>(42);
		}
	};
}

int main() {
	const int value = MemberTemplateNamespaceExplicitNoArgs::Holder::value<int>();
	return value == 42 ? 0 : 1;
}
