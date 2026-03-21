template<typename U, class C, int N>
struct Box {
	C value;

	int adjust() const;
};

template<typename U, class C, int N>
int Box<U, C, N>::adjust() const {
	return N + static_cast<int>(sizeof(C)) - static_cast<int>(sizeof(U)) + value;
}

int main() {
	Box<char, int, 38> box{1};
	return box.adjust();
}
