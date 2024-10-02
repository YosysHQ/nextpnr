#include <boost/algorithm/string.hpp>
#include <regex>
#include <utility>

#include "log.h"
#include "nextpnr.h"
#include "util.h"

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include "cst.h"
#include "gowin.h"

NEXTPNR_NAMESPACE_BEGIN

struct GowinCstReader
{
    Context *ctx;
    std::istream &in;

    GowinCstReader(Context *ctx, std::istream &in) : ctx(ctx), in(in){};

    const PadInfoPOD *pinLookup(const PadInfoPOD *list, const size_t len, const IdString idx)
    {
        for (size_t i = 0; i < len; i++) {
            const PadInfoPOD *pin = &list[i];
            if (IdString(pin->package_pin) == idx) {
                return pin;
            }
        }
        return nullptr;
    }

    Loc getLoc(std::smatch match, int maxX, int maxY)
    {
        int col = std::stoi(match[2]);
        int row = 1; // Top
        std::string side = match[1].str();
        if (side == "R") {
            row = col;
            col = maxX;
        } else if (side == "B") {
            row = maxY;
        } else if (side == "L") {
            row = col;
            col = 1;
        }
        int z = match[3].str()[0] - 'A' + BelZ::IOBA_Z;
        return Loc(col - 1, row - 1, z);
    }

    BelId getConstrainedHCLKBel(std::smatch match, int maxX, int maxY)
    {
        int idx = std::stoi(match[3]);
        int bel_z = BelZ::CLKDIV_0_Z + 2 * idx;

        std::string side = match[2].str();
        bool lr = (side == "LEFT") || (side == "RIGHT");
        int y_coord = (side == "BOTTOM") ? maxY - 1 : 0;
        int x_coord = (side == "RIGHT") ? maxX - 1 : 0;

        for (auto &bel : ctx->getBelsInBucket(ctx->getBelBucketForCellType(id_CLKDIV))) {
            auto this_loc = ctx->getBelLocation(bel);
            if (lr && this_loc.x == x_coord && this_loc.z == bel_z && this_loc.y != 0 &&
                this_loc.y != maxY - 1) // left or right side
                return bel;
            else if (!lr && this_loc.y == y_coord && this_loc.z == bel_z) // top or bottom side
                return bel;
        }
        return BelId();
    }

    bool run(void)
    {
        pool<std::pair<IdString, IdStringList>> constrained_cells;
        auto debug_cell = [this, &constrained_cells](IdString cellId, IdStringList belId) {
            if (ctx->debug) {
                constrained_cells.insert(std::make_pair(cellId, belId));
            }
        };

        log_info("Reading constraints...\n");
        try {
            // If two locations are specified separated by commas (for differential I/O buffers),
            // only the first location is actually recognized and used.
            // And pin A will be Positive and pin B will be Negative in any case.
            std::regex iobre = std::regex("IO_LOC +\"([^\"]+)\" +([^ ,;]+)(, *[^ ;]+)? *;.*[\\s\\S]*");
            std::regex portre = std::regex("IO_PORT +\"([^\"]+)\" +([^;]+;).*[\\s\\S]*");
            std::regex port_attrre = std::regex("([^ =;]+=[^ =;]+) *([^;]*;)");
            std::regex iobelre = std::regex("IO([TRBL])([0-9]+)\\[?([A-Z])\\]?");
            std::regex inslocre =
                    std::regex("INS_LOC +\"([^\"]+)\" +R([0-9]+)C([0-9]+)\\[([0-9])\\]\\[([AB])\\] *;.*[\\s\\S]*");
            std::regex hclkre =
                    std::regex("INS_LOC +\"([^\"]+)\" +(TOP|RIGHT|BOTTOM|LEFT)SIDE\\[([0,1])\\] *;*[\\s\\S]*");
            std::regex clockre = std::regex("CLOCK_LOC +\"([^\"]+)\" +BUF([GS])(\\[([0-7])\\])?[^;]*;.*[\\s\\S]*");
            std::smatch match, match_attr, match_pinloc;
            std::string line, pinline;
            std::vector<IdStringList> constrained_clkdivs;
            enum
            {
                ioloc,
                ioport,
                insloc,
                clock,
                hclk
            } cst_type;

            while (!in.eof()) {
                std::getline(in, line);
                cst_type = ioloc;
                if (!std::regex_match(line, match, iobre)) {
                    if (std::regex_match(line, match, portre)) {
                        cst_type = ioport;
                    } else {
                        if (std::regex_match(line, match, clockre)) {
                            cst_type = clock;
                        } else {
                            if (std::regex_match(line, match, inslocre)) {
                                cst_type = insloc;
                            } else {
                                if (std::regex_match(line, match, hclkre)) {
                                    cst_type = hclk;
                                } else {
                                    if ((!line.empty()) && (line.rfind("//", 0) == std::string::npos)) {
                                        log_warning("Invalid constraint: %s\n", line.c_str());
                                    }
                                    continue;
                                }
                            }
                        }
                    }
                }

                IdString net = ctx->id(match[1]);
                auto it = ctx->cells.find(net);
                if (cst_type != clock && it == ctx->cells.end()) {
                    log_info("Cell %s not found\n", net.c_str(ctx));
                    continue;
                }
                switch (cst_type) {
                case clock: { // CLOCK name BUFG|S=#
                    std::string which_clock = match[2];
                    std::string lw = match[4];
                    int lw_idx = -1;
                    if (lw.length() > 0) {
                        lw_idx = atoi(lw.c_str());
                        log_info("lw_idx:%d\n", lw_idx);
                    }
                    if (which_clock.at(0) == 'S') {
                        auto ni = ctx->nets.find(net);
                        if (ni == ctx->nets.end()) {
                            log_info("Net %s not found\n", net.c_str(ctx));
                            continue;
                        }
                        // if (!allocate_longwire(ni->second.get(), lw_idx)) {
                        log_info("Can't use the long wires. The %s network will use normal routing.\n", net.c_str(ctx));
                        //}
                    } else {
                        auto ni = ctx->nets.find(net);
                        if (ni == ctx->nets.end()) {
                            log_info("Net %s not found\n", net.c_str(ctx));
                            continue;
                        }
                        if (ctx->debug) {
                            log_info("Mark net '%s' as CLOCK\n", net.c_str(ctx));
                        }
                        // XXX YES for now. May be put the number here
                        ni->second->attrs[id_CLOCK] = Property(std::string("YES"));
                    }
                } break;
                case ioloc: { // IO_LOC name pin
                    IdString pinname = ctx->id(match[2]);
                    pinline = match[2];

                    const PadInfoPOD *belname =
                            pinLookup(ctx->package_info->pads.get(), ctx->package_info->pads.ssize(), pinname);
                    if (belname != nullptr) {
                        IdStringList bel = IdStringList::concat(IdString(belname->tile), IdString(belname->bel));
                        it->second->setAttr(IdString(ID_BEL), bel.str(ctx));
                        debug_cell(it->second->name, bel);
                    } else {
                        if (std::regex_match(pinline, match_pinloc, iobelre)) {
                            // may be it's IOx#[AB] style?
                            Loc loc = getLoc(match_pinloc, ctx->getGridDimX(), ctx->getGridDimY());
                            BelId bel = ctx->getBelByLocation(loc);
                            if (bel == BelId()) {
                                log_error("Pin %s not found (TRBL style). \n", pinline.c_str());
                            }
                            it->second->setAttr(IdString(ID_BEL), std::string(ctx->nameOfBel(bel)));
                            debug_cell(it->second->name, ctx->getBelName(bel));
                        } else {
                            log_error("Pin %s not found (pin# style)\n", pinname.c_str(ctx));
                        }
                    }
                } break;
                case hclk: {
                    IdString cell_type = it->second->type;
                    if (cell_type != id_CLKDIV) {
                        log_error("Unsupported or invalid cell type %s for hclk\n", cell_type.c_str(ctx));
                    }
                    BelId hclk_bel = getConstrainedHCLKBel(match, ctx->getGridDimX(), ctx->getGridDimY());
                    if (hclk_bel != BelId()) {
                        auto hclk_bel_name = ctx->getBelName(hclk_bel);
                        if (std::find(constrained_clkdivs.begin(), constrained_clkdivs.end(), hclk_bel_name) !=
                            constrained_clkdivs.end()) {
                            log_error("Only one CLKDIV can be placed at %sSIDE[%s]\n", match[2].str().c_str(),
                                      match[3].str().c_str());
                        }
                        constrained_clkdivs.push_back(hclk_bel_name);
                        it->second->setAttr(id_BEL, ctx->getBelName(hclk_bel).str(ctx));
                        debug_cell(it->second->name, ctx->getBelName(hclk_bel));
                    } else {
                        log_error("No Bel of type CLKDIV found at constrained location %sSIDE[%s]\n",
                                  match[2].str().c_str(), match[3].str().c_str());
                    }
                } break;
                default: { // IO_PORT attr=value
                    std::string attr_val = match[2];
                    while (std::regex_match(attr_val, match_attr, port_attrre)) {
                        std::string attr = "&";
                        attr += match_attr[1];
                        boost::algorithm::to_upper(attr);
                        it->second->setAttr(ctx->id(attr), 1);
                        attr_val = match_attr[2];
                    }
                }
                }
            }
            if (ctx->debug) {
                for (auto &cell : constrained_cells) {
                    log_info("Cell %s is constrained to %s\n", cell.first.c_str(ctx), cell.second.str(ctx).c_str());
                }
            }
            return true;
        } catch (log_execution_error_exception) {
            return false;
        }
    }
};

bool gowin_apply_constraints(Context *ctx, std::istream &in)
{
    GowinCstReader reader(ctx, in);
    return reader.run();
}

NEXTPNR_NAMESPACE_END
