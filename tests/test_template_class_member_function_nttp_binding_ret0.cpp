template <int V>
struct Reader {
	template <int K>
	constexpr int read() const {
		return V + K == 3 ? 0 : 1;
	}
};

int main() {
	return Reader<1>{}.read<2>();
}
