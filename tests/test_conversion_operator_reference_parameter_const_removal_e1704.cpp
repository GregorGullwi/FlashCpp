struct ConstRefConversion {
	operator const int&() const {
		static int value{42};
		return value;
	}
};

void set_seven(int& value);

int main() {
	set_seven(ConstRefConversion{});
	return 0;
}
