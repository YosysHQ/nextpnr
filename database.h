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

struct BelId
{
	uint8_t tile_x = 0, tile_y = 0;
	uint16_t index = 0;

	bool nil() const {
		return !tile_x && !tile_y && !index;
	}
} __attribute__((packed));

struct WireId
{
	uint8_t tile_x = 0, tile_y = 0;
	uint16_t index = 0;

	bool nil() const {
		return !tile_x && !tile_y && !index;
	}
} __attribute__((packed));

namespace std
{
	template<> struct hash<BelId>
        {
		std::size_t operator()(const BelId &bel) const noexcept
		{
			std::size_t result = std::hash<int>{}(bel.index);
			result ^= std::hash<int>{}(bel.tile_x) + 0x9e3779b9 + (result << 6) + (result >> 2);
			result ^= std::hash<int>{}(bel.tile_y) + 0x9e3779b9 + (result << 6) + (result >> 2);
			return result;
		}
	};

	template<> struct hash<WireId>
        {
		std::size_t operator()(const WireId &wire) const noexcept
		{
			std::size_t result = std::hash<int>{}(wire.index);
			result ^= std::hash<int>{}(wire.tile_x) + 0x9e3779b9 + (result << 6) + (result >> 2);
			result ^= std::hash<int>{}(wire.tile_y) + 0x9e3779b9 + (result << 6) + (result >> 2);
			return result;
		}
	};
}

struct BelIterator
{
	BelId *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const BelIterator &other) const { return ptr != other.ptr; }
	BelId operator*() const { return *ptr; }
};

struct BelRange
{
	BelIterator b, e;
	BelIterator begin() const { return b; }
	BelIterator end() const { return e; }
};

struct WireIterator
{
	WireId *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const WireIterator &other) const { return ptr != other.ptr; }
	WireId operator*() const { return *ptr; }
};

struct WireRange
{
	WireIterator b, e;
	WireIterator begin() const { return b; }
	WireIterator end() const { return e; }
};

struct WireDelay
{
	WireId wire;
	float delay;
};

struct WireDelayIterator
{
	WireDelay *ptr = nullptr;

	void operator++() { ptr++; }
	bool operator!=(const WireDelayIterator &other) const { return ptr != other.ptr; }
	WireDelay operator*() const { return *ptr; }
};

struct WireDelayRange
{
	WireDelayIterator b, e;
	WireDelayIterator begin() const { return b; }
	WireDelayIterator end() const { return e; }
};

struct BelPin
{
	BelId bel;
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

struct ChipArgs
{
	// ...
};

struct Chip
{
	// ...

	Chip(ChipArgs args);

	void setBelActive(BelId bel, bool active);
	bool getBelActive(BelId bel);

	BelId getBelByName(IdString name) const;
	WireId getWireByName(IdString name) const;
	IdString getBelName(BelId bel) const;
	IdString getWireName(WireId wire) const;

	BelRange getBels() const;
	BelRange getBelsByType(IdString type) const;
	IdString getBelType(BelId bel) const;

	void getBelPosition(BelId bel, float &x, float &y) const;
	void getWirePosition(WireId wire, float &x, float &y) const;
	vector<GuiLine> getBelGuiLines(BelId bel) const;
	vector<GuiLine> getWireGuiLines(WireId wire) const;

	WireRange getWires() const;
	WireDelayRange getWiresUphill(WireId wire) const;
	WireDelayRange getWiresDownhill(WireId wire) const;
	WireDelayRange getWiresBidir(WireId wire) const;
	WireDelayRange getWireAliases(WireId wire) const;

	// the following will only operate on / return "active" BELs
	// multiple active uphill BELs for a wire will cause a runtime error
	WireId getWireBelPin(BelId bel, IdString pin) const;
	BelPin getBelPinUphill(WireId wire) const;
	BelPinRange getBelPinsDownhill(WireId wire) const;
};

// -------------------------------------------------------
// Generic declarations

struct CellInfo;

struct PortRef
{
	CellInfo *cell;
	IdString port;
};

struct NetInfo
{
	IdString name;
	PortRef driver;
	vector<PortRef> users;
	dict<IdString, std::string> attrs;

	// wire -> (uphill_wire, delay)
	dict<WireId, std::pair<WireId, float>> wires;
};

enum PortType
{
	PORT_IN = 0,
	PORT_OUT = 1,
	PORT_INOUT = 2
};

struct PortInfo
{
	IdString name;
	NetInfo *net;
	PortType type;
};

struct CellInfo
{
	IdString name, type;
	dict<IdString, PortInfo> ports;
	dict<IdString, std::string> attrs, params;

	BelId bel;
	// cell_port -> bel_pin
	dict<IdString, IdString> pins;
};

struct Design
{
	struct Chip chip;

	Design(ChipArgs args) : chip(args) {
		// ...
	}

	dict<IdString, NetInfo*> nets;
	dict<IdString, CellInfo*> cells;
};
