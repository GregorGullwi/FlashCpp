auto bad_auto_return(bool choose_int) {
	if (choose_int) {
		return 1;
	}
	return 1.0;
}

int main() {
	return bad_auto_return(false);
}
