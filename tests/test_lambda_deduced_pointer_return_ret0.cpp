// A lambda without a trailing return type must deduce pointer returns from
// `return &value;`, including static locals, parameters, and members.

struct Target {
	int value;
};

static int global_value = 7;

int main() {
	auto get_global = []() { return &global_value; };
	if (*get_global() != 7) {
		return 1;
	}
	auto get_static = []() {
		static int static_value = 11;
		return &static_value;
	};
	if (*get_static() != 11) {
		return 2;
	}
	auto get_parameter = [](int& value) { return &value; };
	int local_value = 13;
	if (*get_parameter(local_value) != 13) {
		return 3;
	}
	auto get_member = [](Target& target) { return &target.value; };
	Target target{17};
	if (*get_member(target) != 17) {
		return 4;
	}
	return 0;
}
