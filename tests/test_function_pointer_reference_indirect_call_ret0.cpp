typedef int (*Callback)();

int answer() { return 37; }

int invokePointer(Callback const& callback) { return callback(); }
int invokeRvalue(Callback&& callback) { return callback(); }

int main() {
	Callback pointer = answer;
	if (invokePointer(pointer) != 37) return 1;
	if (invokeRvalue(&answer) != 37) return 2;
	return 0;
}
