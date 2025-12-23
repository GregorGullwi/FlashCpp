# Template Instantiation Profiling System

## Overview

This document describes the template instantiation profiling system added to FlashCpp to help identify performance bottlenecks in template-heavy code and standard library headers.

## Motivation

Standard C++ library headers cause 10+ second timeouts when compiling with FlashCpp. The profiling system was implemented to:
1. Identify where time is being spent during template instantiation
2. Measure cache hit/miss rates
3. Find the most expensive templates
4. Guide optimization efforts

## Architecture

### Core Components

**TemplateProfilingStats.h** - Main profiling infrastructure:
- `TemplateProfilingAccumulator` - Tracks count, total time, min/max/mean for operations
- `TemplateProfilingStats` - Singleton that collects all profiling data
- `TemplateProfilingTimer` - RAII timer for automatic profiling
- Compile-time flag `ENABLE_TEMPLATE_PROFILING` to enable/disable (default: enabled)

### Instrumented Code Paths

The following functions in `Parser.cpp` are instrumented:

1. **`try_instantiate_class_template`** - Class template instantiation
   - Overall instantiation timing
   - Cache hit/miss tracking
   - Specialization pattern matching timing
   - Template lookup timing

2. **`try_instantiate_template`** - Function template instantiation
   - Overall instantiation timing
   - Cache hit/miss tracking

### Tracked Metrics

#### Overall Breakdown
- **Template Lookups** - Time spent looking up template definitions
- **Template Parsing** - Time spent parsing template bodies (future)
- **Type Substitution** - Time spent substituting template parameters (future)
- **Specialization Matching** - Time spent finding the best specialization

#### Cache Statistics
- **Cache Hits** - Templates found already instantiated
- **Cache Misses** - Templates requiring new instantiation
- **Hit Rate** - Percentage of cache hits

#### Top Templates
- **Most Instantiated** - Templates instantiated most frequently
- **Slowest** - Templates taking the most total time

## Usage

### Enabling Profiling

Profiling is automatically enabled when compiling with the `--timing` or `--time` flag:

```bash
./FlashCpp myfile.cpp -o myfile.o --timing
```

### Output Example

```
=== Template Instantiation Profiling ===

Overall Breakdown:
  Template Lookups              : count=   14, total=   0.001 ms, mean=   0.071 μs
  Specialization Matching       : count=   14, total=   0.018 ms, mean=   1.286 μs

Cache Statistics:
  Cache Hits:   5
  Cache Misses: 14
  Hit Rate:     26.32%

Top 10 Most Instantiated Templates (by count):
   1. Wrapper                                 : count=    9, total=   0.433 ms
   2. Pair                                    : count=    6, total=   0.163 ms

Top 10 Slowest Templates (by total time):
   1. Wrapper                                 : count=    9, total=   0.433 ms
   2. Pair                                    : count=    6, total=   0.163 ms

=== End Template Profiling ===
```

### Disabling Profiling

To completely disable profiling (zero overhead), edit `src/TemplateProfilingStats.h`:

```cpp
#ifndef ENABLE_TEMPLATE_PROFILING
#define ENABLE_TEMPLATE_PROFILING 0  // Change to 0
#endif
```

## Implementation Details

### Profiling Macros

```cpp
PROFILE_TEMPLATE_INSTANTIATION(name)       // Track template instantiation
PROFILE_TEMPLATE_LOOKUP()                   // Track template lookup
PROFILE_TEMPLATE_PARSING()                  // Track template parsing
PROFILE_TEMPLATE_SUBSTITUTION()             // Track type substitution
PROFILE_TEMPLATE_SPECIALIZATION_MATCH()     // Track specialization matching
PROFILE_TEMPLATE_CACHE_HIT(name)            // Record cache hit
PROFILE_TEMPLATE_CACHE_MISS(name)           // Record cache miss
```

### Adding New Profiling Points

To add profiling to a new code path:

1. Include the header:
```cpp
#include "TemplateProfilingStats.h"
```

2. Add profiling at function entry:
```cpp
void my_template_function() {
    PROFILE_TEMPLATE_INSTANTIATION("MyTemplate");
    // ... rest of function
}
```

3. The RAII timer will automatically record timing when the function exits.

## Key Findings

From initial testing with template-intensive code:

### Performance Characteristics
- **Individual instantiations**: 20-50 microseconds per template
- **Lookup operations**: < 1 microsecond
- **Specialization matching**: 1-8 microseconds
- **Cache hit rate**: ~26% (room for improvement)

### Bottleneck Analysis
For standard library headers, the main issue is not individual template speed, but:
1. **Volume** - Hundreds or thousands of instantiations
2. **Complexity** - Deep instantiation chains
3. **Low cache efficiency** - Many unique instantiations

### Existing Optimizations
The codebase already implements several good optimizations:
- ✅ Template instantiation caching (via `gTypesByName` for classes, `TemplateInstantiationKey` for functions)
- ✅ Recursion depth limit (10 levels)
- ✅ Early return for dependent types
- ✅ Pattern-based specialization matching

## Future Work

### Immediate Priorities
1. **Test with standard headers** - Profile actual std::vector, std::array, etc.
2. **Analyze bottlenecks** - Identify which stdlib templates are slowest
3. **Optimize string operations** - Template name generation involves concatenation

### Potential Optimizations
1. **Lazy member instantiation** - Don't instantiate unused member functions
2. **Type resolution caching** - Cache frequently-used type lookups
3. **Instantiation queuing** - Batch independent instantiations
4. **Smarter caching** - Pre-instantiate common patterns

### Known Limitations
- Standard headers still timeout (10+ seconds)
- Not all code paths are instrumented yet (parsing, substitution phases)
- Cache hit rate is lower than ideal

## Testing

### Unit Test
```bash
# Create test file with multiple template instantiations
cat > /tmp/test_templates.cpp << 'EOF'
template<typename T>
struct Wrapper { T value; };

int main() {
    Wrapper<int> w1;
    Wrapper<float> w2;
    return 0;
}
EOF

# Compile with profiling
./x64/Debug/FlashCpp /tmp/test_templates.cpp -o /tmp/test.o --timing
```

### Expected Output
- Profiling data should show 2 Wrapper instantiations
- Cache should show hits for subsequent uses
- Total time should be < 1ms for simple templates

## References

- **STANDARD_HEADERS_MISSING_FEATURES.md** - Original requirements and analysis
- **src/TemplateProfilingStats.h** - Profiling implementation
- **src/Parser.cpp** - Instrumented template instantiation functions
- **src/main.cpp** - Integration with --timing flag

## Changelog

### 2024-12-23
- Initial implementation of profiling system
- Instrumented class and function template instantiation
- Added cache hit/miss tracking
- Integrated with --timing flag in main.cpp
- Tested with template-intensive code (19 instantiations)
