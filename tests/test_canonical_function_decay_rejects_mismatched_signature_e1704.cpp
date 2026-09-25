int accept(int (*callback)(int));
int wrong(float value);

int main() {
	return accept(wrong);
}
