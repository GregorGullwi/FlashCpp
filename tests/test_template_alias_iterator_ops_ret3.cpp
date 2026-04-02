template<typename It>
int distance_like(It first, It last) {
	return (last != first) ? static_cast<int>(last - first) : -1;
}

template<typename T, int N>
struct Buffer {
	using iterator = T*;

	T values[N];

	iterator begin() {
		return values;
	}

	iterator end() {
		return begin() + N;
	}
};

int main() {
	Buffer<int, 3> buffer{{1, 2, 3}};
	return distance_like(buffer.begin(), buffer.end());
}
