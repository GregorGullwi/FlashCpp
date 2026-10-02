int use_shadowed_parameter(int value) {
	{
		int value = 7;
		if (value != 7) return 1;
	}
	return value;
}

int main() {
	return use_shadowed_parameter(3) == 3 ? 0 : 2;
}