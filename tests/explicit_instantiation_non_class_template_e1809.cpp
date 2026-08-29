template <typename T>
void not_a_class_template();

template class not_a_class_template<int>;

int main() {
	return 0;
}
