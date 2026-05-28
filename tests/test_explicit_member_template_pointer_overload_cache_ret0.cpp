template <typename T>
struct Holder {
	template <typename U>
	int pick(T value) {
		return value == 4 ? 1 : 7;
	}

	template <typename U>
	int pick(T* value) {
		return value != nullptr && *value == 4 ? 0 : 9;
	}
};

int main() {
	Holder<int> holder;
	int value = 4;
	return holder.pick<char>(&value);
}
