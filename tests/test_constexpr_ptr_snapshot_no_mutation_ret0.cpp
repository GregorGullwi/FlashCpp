// Regression test: pointer snapshot for &arr[0] must capture the full array,
// not just a single element.  When no mutation occurs after taking the address,
// refreshPointerSnapshotsForBinding is never called, so the initial snapshot
// created at address-of time is the only data available for cross-scope
// dereference.  If only one element was snapshotted, p[1] and p[2] would fail
// with an out-of-bounds error.

constexpr int sumThree(const int* p) {
	return p[0] + p[1] + p[2];
}

constexpr int evaluateWithoutMutation() {
	int arr[3] = {10, 20, 30};
	const int* p = &arr[0];
	// No mutation of arr after taking the pointer — the snapshot must already
	// contain all three elements for sumThree to work.
	return sumThree(p);
}

static_assert(evaluateWithoutMutation() == 60);

int main() {
	return evaluateWithoutMutation() == 60 ? 0 : 1;
}
