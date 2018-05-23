// clang -o demo -Wall -std=c++11 demo.cc database.cc -lstdc++

#include "database.h"

int main()
{
	Design design("default");

	for (auto bel : design.chip.getBels())
		printf("%s\n", design.chip.getObjName(bel).c_str());

	return 0;
}
