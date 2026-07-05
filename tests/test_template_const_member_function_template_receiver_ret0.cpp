template <class T>
struct Box {
	template <class U>
	int equals(const Box<U>&) const {
		return 1;
	}
};

int main() {
	const Box<int> left;
	Box<int> right;
	return left.equals(right) == 1 ? 0 : 1;
}
