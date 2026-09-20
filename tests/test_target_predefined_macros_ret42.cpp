// Run normally for COFF and preprocess with -fmangling itanium for ELF.
#if defined(__ELF__)
#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER) || defined(_MSC_FULL_VER) || defined(_MSVC_LANG) || defined(_M_X64)
#error ELF target exposes Windows or MSVC target macros
#endif
#else
#if !defined(_WIN32) || !defined(_WIN64) || !defined(_MSC_VER)
#error Windows target is missing its platform macros
#endif
#endif

int main() {
	return 42;
}
