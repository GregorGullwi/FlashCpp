struct TemplateDoubleDerefPoint {
	int x;
};

template <class T>
T** pass_slot(T** slot) {
	return slot;
}

int main() {
	TemplateDoubleDerefPoint point = {7};
	TemplateDoubleDerefPoint* pointer = &point;
	return (*pass_slot(&pointer))->x == 7 ? 0 : 1;
}
