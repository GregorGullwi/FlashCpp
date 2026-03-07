extern "C" int asm_function_target_impl() {
	return 42;
}

extern "C" int asm_function_target_alias() __asm__("asm_function_target_impl");

int main() {
	#ifdef __asm
	return 1;
	#elif defined(__asm__)
	return 2;
	#else
	return asm_function_target_alias();
	#endif
}
