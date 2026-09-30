struct ShortConvertible {
	operator short() const {
		return 17;
	}
};

int choose(int value) {
	return value == 17 ? 0 : 1;
}

long choose(long) {
	return 2;
}

int main() {
	return choose(ShortConvertible{});
}
