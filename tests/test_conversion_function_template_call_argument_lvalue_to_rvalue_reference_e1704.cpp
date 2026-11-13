struct ReferenceTemplateConversion {
	template<class U>
	operator U&() const {
		static U value{42};
		return value;
	}
};

void set_seven(int&& value);

int main() {
	set_seven(ReferenceTemplateConversion{});
	return 0;
}
