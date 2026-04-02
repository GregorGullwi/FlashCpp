template<typename T>
struct Box {
	T value;
};

template<typename T>
bool operator==(const Box<T>& lhs, const Box<T>& rhs) {
	return lhs.value == rhs.value;
}

int main() {
	Box<int> lhs{7};
	Box<int> rhs{7};
	return (lhs == rhs) ? 0 : 1;
}
