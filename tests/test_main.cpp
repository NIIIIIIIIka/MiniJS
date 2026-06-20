#include "minijs/diagnostic.h"
#include "minijs/source_location.h"

#include <iostream>
#include <string>

namespace
{
int failures = 0;

void expect(bool condition, const char* expression, int line)
{
    if (condition)
    {
        return;
    }

    std::cerr << "test failure at line " << line << ": " << expression << '\n';
    ++failures;
}
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

int main()
{
    const minijs::SourceLocation defaultLocation;
    EXPECT(defaultLocation.offset == 0);
    EXPECT(defaultLocation.line == 1);
    EXPECT(defaultLocation.column == 1);

    const minijs::SourceLocation location{12, 3, 5};
    const minijs::Diagnostic diagnostic(location, "unexpected token");
    EXPECT(diagnostic.location == location);
    EXPECT(diagnostic.message == std::string("unexpected token"));

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "all tests passed\n";
    return 0;
}
