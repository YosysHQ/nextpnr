#include <unistd.h>
#include <assert.h>
int main()
{
    char buffer[512];
    int i, j;
    while (1) {
        i = read(0, buffer, 512);
        if (i == 0) break;
        assert(i > 0);
        j = write(1, buffer, i);
        assert(i == j);
    }
    return 0;
}
