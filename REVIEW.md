# Compliance
All code should be compliant with the C++20 standard. Evaluate everything through the language specification and grammar lense.

# Formatting
All `.h` and `.cpp` source files use **hard tabs** (not spaces) for indentation.

# Auto fix
Suggest auto fixes when possible. But be careful when crafting the diff so all the brackets and indentation line upp correctly.

# Performane
Keep your eye out for bad performance characteristics and suggest performant solutions.

# Architechure
How does the change fit into the overall compiler and architecture? Is code generation doing lookups or fallbacks due missing logic in the semantic analyser? Or is the parser doing work that the semantic analyzer should be doing?
