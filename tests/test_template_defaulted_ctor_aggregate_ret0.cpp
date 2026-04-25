// C++20: A template struct with a user-declared (= default) constructor is NOT an aggregate.
// is_explicitly_defaulted must be propagated during template constructor cloning.
template<typename T>
struct HasDefaulted {
	T val;
	constexpr HasDefaulted() = default;
};

extern "C" int printf(const char*, ...);
int main() {
	int is_agg_int  = __is_aggregate(HasDefaulted<int>);
	int is_agg_char = __is_aggregate(HasDefaulted<char>);
	printf("HasDefaulted<int> aggregate=%d(expect 0)\n",  is_agg_int);
	printf("HasDefaulted<char> aggregate=%d(expect 0)\n", is_agg_char);
	return is_agg_int + is_agg_char;  // 0 = pass
}
