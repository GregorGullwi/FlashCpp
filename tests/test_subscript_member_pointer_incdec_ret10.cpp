struct Box {
	int values[3];
	int* ptr;
};

int main() {
	Box boxes[1]{};
	boxes[0].values[0] = 4;
	boxes[0].values[1] = 5;
	boxes[0].values[2] = 6;
	boxes[0].ptr = &boxes[0].values[0];

	int first = *boxes[0].ptr++;
	int second = *++boxes[0].ptr;
	return first + second;
}
