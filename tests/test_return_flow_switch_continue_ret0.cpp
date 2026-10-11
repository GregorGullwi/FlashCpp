int main() {
	int value = 0;
	int total = 0;
	while (value < 4) {
		++value;
		switch (value) {
		case 2:
			continue;
		default:
			break;
		}
		total += value;
	}
	return total == 8 ? 0 : 1;
}