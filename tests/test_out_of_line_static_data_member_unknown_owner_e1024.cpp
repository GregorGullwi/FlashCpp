// A static data-member definition qualified by a class that does not exist is
// invalid and must report OutOfLineDefinitionUnknownClass (1024).

int Missing::data;

int main() {
	return 0;
}
