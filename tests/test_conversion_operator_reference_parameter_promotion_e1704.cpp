struct ShortRefConversion {
	operator short&() const {
		static short value{42};
		return value;
	}
};

void set_seven(int& value);

int main() {
	set_seven(ShortRefConversion{});
	return 0;
}
