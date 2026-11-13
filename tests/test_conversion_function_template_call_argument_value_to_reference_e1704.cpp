struct ValueTemplateConversion {
	template<class U>
	operator U() const {
		return U{};
	}
};

void set_seven(int& value);

int main() {
	set_seven(ValueTemplateConversion{});
	return 0;
}
