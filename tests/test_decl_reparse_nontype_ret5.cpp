// Test: deduced non-type template parameter with auto trailing return type.
// This exercises the declaration-reparse path (should_reparse=true for auto/void
// return placeholder) inside try_instantiate_single_template.  Before the fix,
// the TypeInfo registration loops at the re-parse sites did not skip is_value entries
// from template_args_as_type_args, so non-type params like N=5 were erroneously
// registered in gTypesByName with base_type=Int, corrupting the type table and
// causing incorrect return values (e.g. 127 instead of 5).
template<typename T, int N>
struct Array { T data[N]; };

template<typename T, int N>
auto getSize(Array<T, N>& arr) -> int { return N; }

int main() {
	Array<int, 5> arr;
	return getSize(arr);  // T=int, N=5 deduced from arr; should return 5
}
