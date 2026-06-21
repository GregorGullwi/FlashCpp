using Null = decltype(nullptr);

int bindNullLRef(Null& value) {
	return value == nullptr ? 1 : 0;
}

int main() {
	return bindNullLRef(0);
}
