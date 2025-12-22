// Test goto statements and labels

// Test 1: Simple goto forward
int test_simple_goto_forward() {
    int x = 0;
    goto skip;
    x = 10;
skip:
    x = x + 5;
    return x;  // Should return 5
}

// Test 2: Simple goto backward
int test_simple_goto_backward() {
    int x = 0;
    int count = 0;
start:
    x = x + 1;
    count = count + 1;
    if (count < 3) {
        goto start;
    }
    return x;  // Should return 3
}

// Test 3: Goto with conditional
int test_goto_conditional(int n) {
    int result = 0;
    if (n > 0) {
        goto positive;
    }
    result = -1;
    goto end;
positive:
    result = 1;
end:
    return result;
}

// Test 4: Multiple labels
int test_multiple_labels(int x) {
    int result = 0;
    if (x == 1) {
        goto label1;
    }
    if (x == 2) {
        goto label2;
    }
    if (x == 3) {
        goto label3;
    }
    result = 0;
    goto end;
label1:
    result = 10;
    goto end;
label2:
    result = 20;
    goto end;
label3:
    result = 30;
end:
    return result;
}

// Test 5: Goto in loop
int test_goto_in_loop() {
    int sum = 0;
    int i = 0;
loop_start:
    if (i >= 5) {
        goto loop_end;
    }
    sum = sum + i;
    i = i + 1;
    goto loop_start;
loop_end:
    return sum;  // Should return 0+1+2+3+4 = 10
}

// Test 6: Nested goto
int test_nested_goto(int x) {
    int result = 0;
    if (x > 0) {
        if (x > 10) {
            goto large;
        }
        goto small;
    }
    goto zero;
large:
    result = 100;
    goto end;
small:
    result = 10;
    goto end;
zero:
    result = 0;
end:
    return result;
}


int main() {
    return test_simple_goto_forward();
}
