struct ExplicitTemplateConversion {
	template<class U>
	explicit operator U() const {
		return U{1};
	}
};

int main() {
	int value = ExplicitTemplateConversion{};
	return value;
}
