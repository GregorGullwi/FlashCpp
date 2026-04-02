using Ptr = int*;
using PtrAlias = Ptr;

using Ref = int&;
using RefAlias = Ref;

using Arr = int[3];
using ArrAlias = Arr;

int twice(int x) { return x * 2; }

using Fn = int (*)(int);
using FnAlias = Fn;

int main() {
	if (!__is_pointer(PtrAlias))
		return 1;
	if (__is_pointer(int))
		return 2;

	if (!__is_reference(RefAlias))
		return 3;
	if (__is_reference(PtrAlias))
		return 4;

	if (!__is_bounded_array(ArrAlias))
		return 5;
	if (__is_unbounded_array(ArrAlias))
		return 6;

	FnAlias fn = twice;
	if (fn(4) != 8)
		return 7;

	return 0;
}
