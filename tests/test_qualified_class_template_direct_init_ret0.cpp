namespace demo {
	template<typename T>
	struct SpanLike {
		T* data;
		int size;

		SpanLike(T* data_ptr, int count)
			: data(data_ptr), size(count) {}
	};
}

int main() {
	int values[3] = {42, 1, 0};
	demo::SpanLike<int> span(values, 3);
	(void)span;
	return 0;
}
