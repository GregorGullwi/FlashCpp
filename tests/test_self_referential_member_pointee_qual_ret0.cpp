struct Node {
	const Node* next;
};

int choose(const Node*) { return 1; }
int choose(Node*) { return 2; }

struct Later;
struct Earlier {
	const Later* value;
};
struct Later { int value; };

int choose(const Later*) { return 4; }
int choose(Later*) { return 8; }

int main() {
	Node node{nullptr};
	Later later{7};
	Earlier earlier{&later};
	return choose(node.next) + choose(earlier.value) == 5 ? 0 : 1;
}
