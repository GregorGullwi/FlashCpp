template <class T>
struct ConstMemberTemplate {
	const T value;
};

int main() {
	ConstMemberTemplate<int> object;
	return object.value;
}
