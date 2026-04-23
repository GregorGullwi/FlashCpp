// Scenario B: non-type pack that appears only in explicit template arguments, not in
// the function signature.  sizeof...(Indices) must equal the number of explicit
// non-type args, regardless of what the single function parameter is.
//
// sum_indexed<int, 0, 1, 2>(10):
//   T=int (explicit), Indices={0,1,2} (explicit non-type pack, no function param)
//   returns a + sizeof...(Indices)  =  10 + 3  =  13

template<typename T, int... Indices>
int sum_indexed(T a) { return static_cast<int>(a) + static_cast<int>(sizeof...(Indices)); }

int main() {
	int b = sum_indexed<int, 0, 1, 2>(10);
	if (b != 13) return 1;
	return 0;
}
