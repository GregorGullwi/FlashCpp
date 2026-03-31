struct Tracker {
	int id;
	bool copied;
	bool moved;

	Tracker(int value) : id(value), copied(false), moved(false) {}
	Tracker(const Tracker& other) : id(other.id), copied(true), moved(false) {}
	Tracker(Tracker&& other) : id(other.id), copied(false), moved(true) {}
};

int main() {
	Tracker source(10);
	Tracker copied(source);
	if (!copied.copied)
		return 1;
	if (copied.id != 10)
		return 2;

	Tracker moved(static_cast<Tracker&&>(source));
	if (!moved.moved)
		return 3;
	if (moved.id != 10)
		return 4;

	return 0;
}
