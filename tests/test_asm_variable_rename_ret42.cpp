extern "C" int asm_variable_target_impl = 42;

extern "C" int asm_variable_target_alias __asm__("asm_variable_target_impl");

int main() {
#ifdef __asm
	return 1;
#elif defined(__asm__)
	return 2;
#else
	return asm_variable_target_alias;
#endif
}
