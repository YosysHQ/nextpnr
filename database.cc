#include "database.h"

Chip::Chip(std::string)
{
}

ObjRange Chip::getBels() const
{
	return ObjRange();
}

IdString Chip::getObjName(ObjId obj) const
{
	return "*unknown*";
}
