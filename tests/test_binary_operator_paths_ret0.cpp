int global_bits = 6;
int global_shift = 3;

struct Values {
	int bits;
	int shift;
};

int main() {
	int local_bits = 5;
	int local_shift = 2;
	int* indirect_bits = &local_bits;
	int* indirect_shift = &local_shift;
	Values values{6, 3};

	int global_result = (global_bits & 2) | (global_bits ^ 1);
	int local_result = (local_bits & 1) | (local_bits ^ 2);
	int member_result = (values.bits << 1) >> 1;
	int indirect_result = (*indirect_bits << 2) >> 2;
	int global_shift_result = (global_shift << 2) >> 1;

	return global_result == 7 &&
			   local_result == 7 &&
			   member_result == 6 &&
			   indirect_result == 5 &&
			   global_shift_result == 6
		   ? 0
		   : 1;
}
