// Allocating an abstract class with a new-expression is rejected at the
// constructor-call path and must report AbstractClassInstantiation.

struct Abstract {
	virtual void run() = 0;
};

int main() {
	Abstract* pointer = new Abstract();
	delete pointer;
	return 0;
}
