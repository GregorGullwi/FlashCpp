#include <type_traits>

struct Incomplete;

static_assert(std::__is_complete_or_unbounded(std::__type_identity<int>{}));
static_assert(!std::__is_complete_or_unbounded(std::__type_identity<Incomplete>{}));

int main() {
	return 0;
}
