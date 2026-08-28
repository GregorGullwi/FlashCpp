struct Number {
	int value;
};

Number operator+(Number lhs, Number rhs = Number{40}) {
	return Number{lhs.value + rhs.value};
}

int main() {
	Number value{2};
	return (value + value).value;
}
