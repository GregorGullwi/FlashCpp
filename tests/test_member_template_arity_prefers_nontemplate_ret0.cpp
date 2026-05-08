struct MiniView {
	const char* data() const {
		return "abc";
	}

	unsigned long size() const {
		return 3;
	}
};

struct Holder {
	int find(const char*, unsigned long, unsigned long) const {
		return 42;
	}

	template<typename T>
	int find(const T& value, unsigned long pos = 0) const {
		return find(value.data(), pos, value.size());
	}
};

int main() {
	Holder holder;
	MiniView view;
	return holder.find(view) == 42 ? 0 : 1;
}
