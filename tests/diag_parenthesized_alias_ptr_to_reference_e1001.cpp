using Reference = short&;
using Chained = Reference;
int main() {
	return sizeof(Chained(*));
}
