struct Payload {
	int value;
	short tag;
};

struct LargePayload {
	int value;
	short tag;
	long long wide;
	char flag;
};

int direct_update(const Payload& payload, int& result) {
	result += payload.value + payload.tag;
	return result;
}

int indirect_update(const Payload& payload, int& result) {
	result += payload.value - payload.tag;
	return result;
}

int direct_large_update(LargePayload payload, int& result) {
	result += payload.value + payload.tag + static_cast<int>(payload.wide) + payload.flag;
	return result;
}

int indirect_large_update(LargePayload payload, int& result) {
	result += payload.value - payload.tag + static_cast<int>(payload.wide) - payload.flag;
	return result;
}

int increment(int& value) {
	value += 1;
	return value;
}

int main() {
	Payload payload = {4, 5};
	int result = 1;
	if (direct_update(payload, result) != 10 || result != 10) return 1;
	if (increment(payload.value) != 5 || payload.value != 5) return 2;

	{
		Payload payload = {20, 3};
		int result = 2;
		int (*update)(const Payload&, int&) = indirect_update;
		if (update(payload, result) != 19 || result != 19) return 3;
	}

	LargePayload large = {2, 1, 10, 3};
	int large_result = 4;
	if (direct_large_update(large, large_result) != 20 || large_result != 20) return 4;
	{
		LargePayload large = {20, 3, 100, 2};
		int large_result = 5;
		int (*update)(LargePayload, int&) = indirect_large_update;
		if (update(large, large_result) != 120 || large_result != 120) return 5;
	}

	if (result != 10) return 6;
	if (large_result != 20) return 7;
	return 0;
}
