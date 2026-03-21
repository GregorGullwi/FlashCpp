template<typename A, typename B>
struct Box {
	int size() {
		return 1;
	}
};

template<typename T>
struct Box<int, T> {
	int size() {
		return sizeof(T);
	}
};

int main() {
	Box<int, double> value;
	return value.size();
}
