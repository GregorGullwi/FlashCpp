struct S {
	int value() {
		return 7;
	}
};

int main() {
	S s;
	return s.template value<int>();
}
