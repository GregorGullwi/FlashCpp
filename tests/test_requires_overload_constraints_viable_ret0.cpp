template <class T>
int select(T)
	requires(__is_same(T, int))
{
	return 1;
}

template <class T>
int select(T)
	requires(__is_same(T, char))
{
	return 2;
}

template <class T>
concept CanSelect = requires(T value) {
	select(value);
};

int main() {
	return CanSelect<int> && CanSelect<char> && !CanSelect<long> ? 0 : 1;
}
