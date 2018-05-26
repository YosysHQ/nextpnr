#include "chip.h"

Chip::Chip(ChipArgs)
{
}

BelRange Chip::getBels() const
{
	return BelRange();
}

IdString Chip::getBelName(BelId bel) const
{
	return "*unknown*";
}
