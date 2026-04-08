template<typename T>
struct Box {
	int tag;
	T value;

	bool operator==(const Box& other) const;
};

int main() {
	using EqType = decltype(Box<int>{} == Box<int>{});
	return sizeof(EqType) == sizeof(bool) ? 0 : 1;
}
