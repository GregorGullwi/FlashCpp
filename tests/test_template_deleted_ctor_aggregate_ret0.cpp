// C++20: A template struct with a user-declared (= delete) constructor is NOT an aggregate.
// has_deleted_constructor must be propagated during template instantiation.
template<typename T>
struct HasDeleted {
struct HasDeleted {
	T val;
	HasDeleted(int) = delete;  // user-declared deleted constructor


extern "C" int printf(const char*, ...);
int main() {
    int is_agg_int  = __is_aggregate(HasDeleted<int>);
    int is_agg_char = __is_aggregate(HasDeleted<char>);
    printf("HasDeleted<int> aggregate=%d(expect 0)\n",  is_agg_int);
    printf("HasDeleted<char> aggregate=%d(expect 0)\n", is_agg_char);
    return is_agg_int + is_agg_char;  // 0 = pass
}
