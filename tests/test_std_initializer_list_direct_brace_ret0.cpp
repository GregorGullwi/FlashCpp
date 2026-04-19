// Valid mock for direct std::initializer_list object list-initialization.
// FlashCpp currently treats the brace elements as ordinary constructor
// arguments instead of creating backing array storage for the initializer_list.

namespace std {
template <typename T>
class initializer_list {
public:
	using size_type = unsigned long;

	const T* first_;
	const T* last_;

	initializer_list(const T* first, const T* last) noexcept : first_(first), last_(last) {}

	size_type size() const noexcept {
		return static_cast<size_type>(last_ - first_);
	}
};
} // namespace std

int main() {
	std::initializer_list<int> values = {1, 2, 3, 4, 5};
	return values.size() == 5 ? 0 : 1;
}
