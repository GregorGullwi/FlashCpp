int first_element(int (&values)[][2]) {
	return values[0][0];
}

int main() {
	int values[2][3] = {};
	return first_element(values);
}
