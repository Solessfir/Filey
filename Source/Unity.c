// Single translation unit for the whole application (spec Section 6.2).

#include <stdio.h>

int main(void)
{
    constexpr int version = 0;
    printf("Filey %d\n", version);
    return 0;
}
