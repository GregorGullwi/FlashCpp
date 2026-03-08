int global_paren(sizeof(global_paren));
int global_brace{sizeof(global_brace)};
int global_copy = sizeof(global_copy), global_comma_paren(sizeof(global_comma_paren)), global_comma_brace{sizeof(global_comma_brace)};

int main() {
	int total = global_paren + global_brace + global_copy + global_comma_paren + global_comma_brace;

	{
		char value = 0;
		{
			int value = sizeof(value);
			total += value;
		}
	}

	{
		char value = 0;
		{
			int value(sizeof(value));
			total += value;
		}
	}

	{
		char value = 0;
		{
			int value{sizeof(value)};
			total += value;
		}
	}

	{
		char value = 0;
		{
			static int value(sizeof(value));
			total += value;
		}
	}

	{
		char first = 0;
		char second = 0;
		char third = 0;
		{
			int first = sizeof(first), second(sizeof(second)), third{sizeof(third)};
			total += first + second + third;
		}
	}

	return total;
}
