namespace std {
enum memory_order { memory_order_relaxed = 7 };

int get_relaxed_order() { return memory_order_relaxed; }
} // namespace std

int main() {
	return std::get_relaxed_order() - 7;
}
