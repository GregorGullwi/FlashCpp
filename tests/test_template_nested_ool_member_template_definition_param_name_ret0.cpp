template<class T>
struct NestedOolMemberTemplateParamName {
	struct Inner {
		template<class U>
		int convert(U);
	};
};

template<class T>
template<class V>
int NestedOolMemberTemplateParamName<T>::Inner::convert(V value) {
	return static_cast<int>(value) + 1;
}

int main() {
	NestedOolMemberTemplateParamName<int>::Inner inner;
	return inner.convert(41) == 42 ? 0 : 1;
}
