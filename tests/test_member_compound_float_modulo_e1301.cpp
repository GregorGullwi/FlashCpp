struct Values {
	float value;
};

int main() {
	Values values{10.0f};
	values.value %= 3.0f;
	return 0;
}
