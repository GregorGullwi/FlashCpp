template <typename T>
struct Box {
	T value;
};

int main() {
	Box<long long> box{42};
	return sizeof(box.value) == sizeof(long long) ? 0 : 1;
}
