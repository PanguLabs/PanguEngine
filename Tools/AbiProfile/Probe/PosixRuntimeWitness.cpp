#include <iostream>
#include <string>

// Independent link-only witness. Neither execute it nor link all Module objects.
int main(int argc, char* argv[])
{
    const std::string argument = argc > 0 ? argv[0] : "";
    std::cout << argument;
    return static_cast<int>(argument.empty());
}
