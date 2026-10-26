#include <iostream>
#include <string>

// Independent CRT/STL link witness. Never link an arbitrary STATIC module's
// unresolved application-owned symbols into the witness.
int main(int argc, char* argv[])
{
    const std::string argument = argc > 0 ? argv[0] : "";
    std::cout << argument;
    return static_cast<int>(argument.empty());
}
