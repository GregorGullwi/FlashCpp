// Test named anonymous union inside typedef struct
// Pattern: typedef struct { union { ... } member_name; } Alias;
// This is common in <wchar.h> and standard library headers

typedef struct
{
  int __count;
  union
  {
    unsigned int __wch;
    char __wchb[4];
  } __value;
} __mbstate_t;

int main() {
    __mbstate_t state;
    state.__count = 0;
    state.__value.__wch = 42;
    return state.__value.__wch;
}
