struct ReferenceTemplateConversion {
	template<class U>
	operator U&() const {
		static U value{42};
		return value;
	}
};

void choose(int& value);
void choose(const int& value);

int main() {
	choose(ReferenceTemplateConversion{});
	return 0;
}
