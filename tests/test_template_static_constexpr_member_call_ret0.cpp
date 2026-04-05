template<typename T>
struct TemplateClock {
	static constexpr int epoch_diff = 42;

	static int read() {
		return epoch_diff;
	}
};

int main() {
	return TemplateClock<int>::read() - 42;
}
