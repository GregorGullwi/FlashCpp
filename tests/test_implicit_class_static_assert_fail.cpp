template<typename T>
struct NeedsIntSized {
	static_assert(sizeof(T) == sizeof(int), "T must match int size");
	int value;
};

NeedsIntSized<long long> global_value{};

int main() {
	return global_value.value;
}
