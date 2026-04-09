template <typename T>
struct Box {
	T value;
};

template <typename T>
using BoxAlias = Box<T>;

int main() {
	return BoxAlias<int>{42}.value;
}
