struct Node {
	Node* next;
};

int main() {
	Node a{};
	Node b = a;
	return b.next == nullptr ? 0 : 1;
}
