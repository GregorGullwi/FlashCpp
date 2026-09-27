template <class T>
struct ConstMemberTemplate {
	T value;
};

template <>
struct ConstMemberTemplate<int> {
	const int value;
};

int main() {
	ConstMemberTemplate<int> object;
	return object.value;
}
