struct Payload {
	int x;
	int y;
};

using callback_t = int(__stdcall*)(int, float, ...) noexcept;
using struct_callback_t = int(__stdcall*)(Payload, float) noexcept;

struct Holder {
	using member_callback_t = Payload(__stdcall*)(Payload, int) noexcept;
	member_callback_t callback;
};

int __stdcall compute_score(int base, float scale, ...) noexcept {
	return base + static_cast<int>(scale * 8.0f);
}

int __stdcall score_payload(Payload value, float scale) noexcept {
	return value.x + value.y + static_cast<int>(scale * 4.0f);
}

Payload __stdcall shift_payload(Payload value, int delta) noexcept {
	value.x += delta;
	value.y += delta * 2;
	return value;
}

int call_callback(callback_t callback, int base, float scale) {
	return callback(base, scale, 7);
}

int call_struct_callback(struct_callback_t callback, Payload value, float scale) {
	return callback(value, scale);
}

int main() {
	callback_t score_callback = compute_score;
	if (call_callback(score_callback, 10, 1.5f) != 22)
		return 1;

	Payload initial{4, 5};
	struct_callback_t payload_callback = score_payload;
	if (call_struct_callback(payload_callback, initial, 1.5f) != 15)
		return 2;

	Holder holder{shift_payload};
	Holder::member_callback_t member_callback = holder.callback;
	Payload shifted = member_callback(initial, 6);
	if (shifted.x != 10 || shifted.y != 17)
		return 3;

	return 0;
}
