int choose(bool flag) {
	return flag ? 1 : 0;
}

auto add_offset(int value) {
	if (choose(false)) {
		return value + 1;
	}
	return value + 2;
}

int main() {
	return add_offset(40) == 42 ? 0 : 1;
}
