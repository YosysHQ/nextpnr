#include <stdint.h>
#include <string>

// replace with proper IdString later
typedef std::string IdString;

// -------------------------------------------------------
// Arch-specific declarations

#ifdef ARCH_ICE40
struct ObjId
{
	uint8_t tile_x = 0, tile_y = 0;
	uint16_t index = 0;

	bool nil() const {
		return !tile_x && !tile_y && !index;
	}
} __attribute__((packed));

struct ObjIterator
{
	// ...
	ObjId operator*() const;
};

struct ObjRange
{
	ObjIterator begin();
	ObjIterator end();
};

struct BelPin
{
	ObjId bel;
	IdString pin;
};

struct BelPinIterator
{
	// ...
	BelPin operator*() const;
};

struct BelPinRange
{
	BelPinIterator begin();
	BelPinIterator end();
};

struct GuiLine
{
	float x1, y1, x2, y2;
};

struct Chip
{
	Chip(std::string cfg);

	ObjId getObjByName(IdString name) const;
	IdString getObjName(ObjId obj) const;
	IdString getBelType(ObjId obj) const;
	ObjRange getBelsByType(IdString type) const;

	void getObjPosition(ObjId obj, float &x, float &y) const;
	vector<GuiLine> getGuiLines(ObjId obj) const;

	ObjRange getWires() const;
	ObjRange getWiresUphillWire(ObjId wire) const;
	ObjRange getWiresDownhillWire(ObjId wire) const;
	ObjRange getWiresBidirWire(ObjId wire) const;
	ObjRange getWireAliases(ObjId wire) const;

	ObjId getWireBelPin(ObjId bel, IdString pin) const;
	BelPin getBelPinUphillWire(ObjId wire) const;
	BelPinRange getBelPinsDownhillWire(ObjId wire) const;
};
#endif

// -------------------------------------------------------
// Generic declarations

struct PortRef
{
	IdString cell_name;
	IdString port_name;
};

struct NetInfo
{
	IdString name;
	PortRef driver;
	vector<PortRef> users;
	dict<IdString, std::string> attrs;

	// wire objid -> signal delay
	dict<ObjId, float> wires;
};

enum PortType
{
	PORT_IN = 0,
	PORT_OUT = 1,
	PORT_INOUT = 2
};

struct PortInfo
{
	IdString name, net;
	PortType type;
};

struct CellInfo
{
	IdString name, type;
	dict<IdString, PortInfo> ports;
	dict<IdString, std::string> attrs, params;

	ObjId bel;
	// port_name -> pin_name
	dict<IdString, IdString> pins;
};

struct Design
{
	struct Chip;

	Design(std::string chipCfg) : Chip(chipCfg) {
		// ...
	}

	dict<IdString, *NetInfo> nets;
	dict<IdString, *CellInfo> cells;
};
