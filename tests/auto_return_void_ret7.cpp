auto no_return() {
}

auto empty_return() {
	return;
}

int main() {
	no_return();
	empty_return();
	return 7;
}
