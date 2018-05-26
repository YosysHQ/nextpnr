#include "database.h"

int main()
{
	Design design(ChipArgs{});

	for (auto bel : design.chip.getBels())
		printf("%s\n", design.chip.getBelName(bel).c_str());

	return 0;
}
