struct ConditionalExplicitConversion {
	template<class U>
	explicit(sizeof(U) > sizeof(int)) operator U() const {
		return U{2};
	}
};

int main() {
	long long value = ConditionalExplicitConversion{};
	return 0;
}
