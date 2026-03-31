// Test using declarations with namespace scope operator

typedef unsigned long long size_t;
struct flashcpp_file_tag;
typedef flashcpp_file_tag FILE;

namespace std {
using ::FILE;
using ::size_t;
} // namespace std

int main() {
	FILE* f = nullptr;
	size_t n = 0;
	return (f == nullptr && n == 0) ? 0 : 1;
}
