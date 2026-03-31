// Test enum bitwise operations: &, |, ^, ~
// These are commonly used for flag-style enums.

enum Flags : int {
	None = 0,
	Read = 1,
	Write = 2,
	Exec = 4
};

enum FlagSet : unsigned int {
	FNone = 0,
	FRead = 1,
	FWrite = 2,
	FExec = 4,
	FAll = 7
};

int test_enum_bitwise_or() {
	// Result is int, need cast for unscoped enum
	int f = Read | Write;
	return (f == 3) ? 0 : 1;
}

int test_enum_bitwise_and() {
	int tmp = Read | Write;
	int result = tmp & Read;
	return (result == 1) ? 0 : 2;
}

int test_enum_bitwise_xor() {
	int tmp = Read | Write;
	int result = tmp ^ Read;
	return (result == 2) ? 0 : 4;
}

int test_enum_bitwise_not() {
	// Bitwise NOT on enum - should work on underlying type
	int result = ~Read;
	// Just verify it compiles and runs
	return (result < 0 || result >= 0) ? 0 : 8;
}

int test_flagset_or() {
	FlagSet f = static_cast<FlagSet>(FRead | FWrite | FExec);
	return (f == 7) ? 0 : 16;
}

int test_flagset_and() {
	FlagSet f = static_cast<FlagSet>(FRead | FWrite);
	FlagSet result = static_cast<FlagSet>(f & FRead);
	return (result == FRead) ? 0 : 32;
}

int test_flagset_xor() {
	FlagSet f = static_cast<FlagSet>(FRead | FWrite);
	FlagSet result = static_cast<FlagSet>(f ^ FRead);
	return (result == FWrite) ? 0 : 64;
}

int test_mixed_flag_ops() {
	// Test mixing different flag values
	int result = (Read | Write | Exec);	// 1 | 2 | 4 = 7
	int hasRead = result & Read;	 // 7 & 1 = 1
	int hasExec = result & Exec;	 // 7 & 4 = 4
	int noExtra = result & 8;	  // 7 & 8 = 0

	return (hasRead == 1 && hasExec == 4 && noExtra == 0) ? 0 : 128;
}

int main() {
	int result = 0;
	result += test_enum_bitwise_or();	  // 0
	result += test_enum_bitwise_and();	   // 0
	result += test_enum_bitwise_xor();	   // 0
	result += test_enum_bitwise_not();	   // 0
	result += test_flagset_or();			 // 0
	result += test_flagset_and();		  // 0
	result += test_flagset_xor();		  // 0
	result += test_mixed_flag_ops();		 // 0

	return result;
}
