// Regression for MSVC atomic headers that invoke _Compiler_barrier() -> _ReadWriteBarrier().
// Keep the call in unevaluated context so this test only checks parsing/sema lookup.
#define _Compiler_barrier() _ReadWriteBarrier()

using barrier_return_t = decltype(_Compiler_barrier());
barrier_return_t* gBarrierProbe = nullptr;

int main() {
	return gBarrierProbe == nullptr ? 0 : 1;
}
