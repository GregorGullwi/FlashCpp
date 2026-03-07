extern int declaredExternValue __asm("renamed_declared_extern_value");
extern int declaredExternFunc() __asm__("renamed_declared_extern_func");

int main() {
	#ifdef __asm
	return 10;
	#elif defined(__asm__)
	return 11;
	#else
	int value = 0;
	int* localPtr __asm("renamed_local_ptr") = &value;
	return *localPtr;
	#endif
}
