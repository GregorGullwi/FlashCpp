// Regression: <utility> transitively uses the __is_complete_or_unbounded helper heavily.
// This test ensures those helper templates no longer fail with unresolved auto
// mangling while still validating the expected semantic result.
#include <utility>
#include <type_traits>

struct Incomplete;

static_assert(std::__is_complete_or_unbounded(std::__type_identity<int>{}));
static_assert(std::__is_complete_or_unbounded(std::__type_identity<int[]>{}));
static_assert(!std::__is_complete_or_unbounded(std::__type_identity<Incomplete>{}));

int main() {
	return 0;
}
