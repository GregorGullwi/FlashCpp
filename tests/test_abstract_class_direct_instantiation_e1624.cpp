// A non-pointer local of an abstract class type cannot be instantiated. The
// rejection must carry the AbstractClassInstantiation owner with the class name
// rather than an unlocated string.

struct Abstract {
	virtual void run() = 0;
};

int main() {
	Abstract object;
	return 0;
}
