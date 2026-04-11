// Test: initialization/binding into unsigned targets applies type-width truncation
// in constexpr evaluation, matching runtime codegen behavior.

constexpr unsigned char global_byte = 300;
static_assert(global_byte == 44);

constexpr unsigned short global_short = 70000;
static_assert(global_short == 4464);

constexpr unsigned char local_byte_value() {
	unsigned char value = 300;
	return value;
}
static_assert(local_byte_value() == 44);

struct ByteHolder {
	unsigned char value;
	constexpr ByteHolder(unsigned char v) : value(v) {}
};

constexpr ByteHolder holder(300);
static_assert(holder.value == 44);

int main() {
	if (global_byte != 44) {
		return 1;
	}
	if (global_short != 4464) {
		return 2;
	}
	if (local_byte_value() != 44) {
		return 3;
	}
	if (holder.value != 44) {
		return 4;
	}
	return 0;
}
