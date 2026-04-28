// Test built-in subscript for a class whose conversion operator returns T** (a
// pointer-to-pointer). The subscript result is a T* (pointer), and dereferencing
// it gives T.  Exercises the multi-pointer-level path in generateArraySubscriptIr.

struct DoublePtr {
	int val_a;
	int val_b;
	int* ptrs[2];

	operator int**() { return &ptrs[0]; }
};

int main() {
	DoublePtr dp;
	dp.val_a = 10;
	dp.val_b = 32;
	dp.ptrs[0] = &dp.val_a;
	dp.ptrs[1] = &dp.val_b;

	// dp[0] -> ptrs[0] -> &val_a  =>  *dp[0] = 10
	// dp[1] -> ptrs[1] -> &val_b  =>  *dp[1] = 32
	return *dp[0] + *dp[1]; // 42
}
