float returnFloat(bool shouldThrow) {
	try {
		if (shouldThrow) {
			throw 1;
		}
		return 1.25f;
	} catch (int) {
		return 7.75f;
	}
}

double returnDouble(bool shouldThrow) {
	try {
		if (shouldThrow) {
			throw 2;
		}
		return 2.5;
	} catch (int) {
		return 11.5;
	}
}

int main() {
	int checks = 0;
	checks += static_cast<int>(returnFloat(false)) == 1 ? 1 : 0;
	checks += static_cast<int>(returnFloat(true)) == 7 ? 1 : 0;
	checks += static_cast<int>(returnDouble(false)) == 2 ? 1 : 0;
	checks += static_cast<int>(returnDouble(true)) == 11 ? 1 : 0;
	return checks == 4 ? 0 : 1;
}