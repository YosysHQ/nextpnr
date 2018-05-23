#include <stdint.h>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

// replace with proper IdString later
typedef std::string IdString;

// replace with haslib later
template<typename T> using pool = std::unordered_set<T>;
template<typename T, typename U> using dict = std::unordered_map<T, U>;
using std::vector;

// -------------------------------------------------------
// Arch-specific declarations

struct ObjId
{
	uint8_t tile_x = 0, tile_y = 0;
	uint16_t index = 0;

	bool nil() const {
		return !tile_x && !tile_y && !index;
	}
} __attribute__((packed));

namespace std
{
	template<> struct hash<ObjId>
        {
		std::size_t operator()(const ObjId &obj) const noexcept
		{
			std::size_t result = std::hash<int>{}(obj.index);
			result ^= std::hash<int>{}(obj.tile_x) + 0x9e3779b9 + (result << 6) + (result >> 2);
			result ^= std::hash<int>{}(obj.tile_y) + 0x9e3779b9 + (result << 6) + (result >> 2);
			return result;
		}
	};
}

struct ObjIterator
{
	ObjId *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const ObjIterator &other) const { return ptr != other.ptr; }
	ObjId operator*() const { return *ptr; }
};

struct ObjRange
{
	ObjIterator b, e;
	ObjIterator begin() const { return b; }
	ObjIterator end() const { return e; }
};

struct BelPin
{
	ObjId bel;
	IdString pin;
};

struct BelPinIterator
{
	BelPin *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }
	BelPin operator*() const { return *ptr; }
};

struct BelPinRange
{
	BelPinIterator b, e;
	BelPinIterator begin() const { return b; }
	BelPinIterator end() const { return e; }
};

struct GuiLine
{
	float x1, y1, x2, y2;
};

struct Chip
{
	// ...

	Chip(std::string cfg);

	void setBelActive(ObjId bel, bool active);
	bool getBelActive(ObjId bel);

	ObjId getObjByName(IdString name) const;
	IdString getObjName(ObjId obj) const;

	ObjRange getBels() const;
	ObjRange getBelsByType(IdString type) const;
	IdString getBelType(ObjId bel) const;

	void getObjPosition(ObjId obj, float &x, float &y) const;
	vector<GuiLine> getGuiLines(ObjId obj) const;

	ObjRange getWires() const;
	ObjRange getWiresUphill(ObjId wire) const;
	ObjRange getWiresDownhill(ObjId wire) const;
	ObjRange getWiresBidir(ObjId wire) const;
	ObjRange getWireAliases(ObjId wire) const;

	// the following will only operate on / return "active" BELs
	// multiple active uphill BELs for a wire will cause a runtime error
	ObjId getWireBelPin(ObjId bel, IdString pin) const;
	BelPin getBelPinUphill(ObjId wire) const;
	BelPinRange getBelPinsDownhill(ObjId wire) const;
};

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

	// wire -> delay
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
	// cell_port -> bel_pin
	dict<IdString, IdString> pins;
};

struct Design
{
	struct Chip chip;

	Design(std::string chipCfg) : chip(chipCfg) {
		// ...
	}

	dict<IdString, NetInfo*> nets;
	dict<IdString, CellInfo*> cells;
};
