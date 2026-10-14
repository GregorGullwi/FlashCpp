int first(int value) { return value + 1; }
int second(int value) { return value + 2; }

struct Receiver {
	int value;
	Receiver(int (* const& callback)(int)) : value(callback(40)) {}
};

int main() {
	bool pick_first = true;
	int (&selected)(int) = pick_first ? first : second;
	if (selected(41) != 42) return 1;
	Receiver receiver(pick_first ? first : second);
	if (receiver.value != 41) return 2;
	pick_first = false;
	Receiver other(pick_first ? first : second);
	if (other.value != 42) return 3;
	return 0;
}
