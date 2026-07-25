// __declspec(selectany) must emit COMDAT SELECT_ANY so duplicate definitions
// from headers and the CRT merge at link time (same mechanism as UCRT's
// _Avx2WmemEnabledWeakValue under <cwchar>/<limits>).

__declspec(selectany) int selectany_shared = 42;

int main() {
	return selectany_shared == 42 ? 0 : 1;
}
