struct S {
	int x;
	int y;
};

template <int S::* P>
int read(const S& s) {
	return s.*P;
}

int main() {
	S s{};
	s.x = 5;
	s.y = 37;
	return read<&S::y>(s) - 37;
}
