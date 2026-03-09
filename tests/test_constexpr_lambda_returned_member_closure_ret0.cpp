struct ReturnedMemberClosureExample {
	int value;

	constexpr auto makeCopied() {
		return [*this]() mutable {
			value += 2;
			return value;
		};
	}
};

constexpr int returned_member_closure_result() {
	ReturnedMemberClosureExample object{40};
	auto first = object.makeCopied();
	int first_call = first();
	auto second = object.makeCopied();
	return first_call + first() + second();
}

static_assert(returned_member_closure_result() == 128);

int main() {
	return 0;
}