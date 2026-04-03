struct S {
	int x;
};

int main() {
	return sizeof(S{0});
}
