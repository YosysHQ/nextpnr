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
        pool<IdString> adc_ios; // bus#/X#Y#

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
            std::regex adcre = std::regex("USE_ADC_SRC +bus([0-9]) +IO([TRBL])([0-9]+) *;.*[\\s\\S]*");
            std::smatch match, match_attr, match_pinloc;
            std::string line, pinlines[2];
            std::vector<IdStringList> constrained_clkdivs;
            enum
            {
                ioloc,
                ioport,
                insloc,
                clock,
                hclk,
                adc
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
                                    if (std::regex_match(line, match, adcre)) {
                                        cst_type = adc;
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
                }

                IdString net = ctx->id(match[1]);
                auto it = ctx->cells.find(net);
                if (cst_type != clock && cst_type != adc && it == ctx->cells.end()) {
                    log_info("Cell %s not found\n", net.c_str(ctx));
                    continue;
                }
                switch (cst_type) {
                case adc: { // USE_ADC_SRC bus# IOLOC
                    int col = std::stoi(match[3]);
                    int row = 1; // Top
                    std::string side = match[2].str();
                    if (side == "R") {
                        row = col;
                        col = ctx->getGridDimX();
                    } else if (side == "B") {
                        row = ctx->getGridDimY();
                    } else if (side == "L") {
                        row = col;
                        col = 1;
                    }
                    adc_ios.insert(ctx->idf("%d/X%dY%d", std::stoi(match[1]), col - 1, row - 1));
                } break;
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
                    int nb_iter = 1;
                    IdString nets[2];

                    // Prepare pinlines and nets (default: one Pin, one LOC).
                    pinlines[0] = match[2];
                    nets[0] = ctx->id(match[1]);

                    // Differential case: one Pin (_p), two LOCs separated by a ','
                    if (match[3].length() > 0) {
                        nb_iter++;
                        // Uses second pin after removing ','
                        pinlines[1] = std::regex_replace(match[3].str(), std::regex("^,"), "");

                        // Replaces _p with _n in pinname.
                        std::string tmp = std::regex_replace(match[1].str(), std::regex("_p$"), "_n");

                        nets[1] = ctx->id(tmp);
                        it = ctx->cells.find(nets[1]);
                        if (cst_type != clock && it == ctx->cells.end()) {
                            log_info("Cell %s not found\n", nets[1].c_str(ctx));
                            continue;
                        }
                    }

                    for (int iter = 0; iter < nb_iter; iter++) {
                        IdString pinname = ctx->id(pinlines[iter]);
                        auto it = ctx->cells.find(nets[iter]);

                        const PadInfoPOD *belname =
                                pinLookup(ctx->package_info->pads.get(), ctx->package_info->pads.ssize(), pinname);
                        if (belname != nullptr) {
                            IdStringList bel = IdStringList::concat(IdString(belname->tile), IdString(belname->bel));
                            it->second->setAttr(IdString(ID_BEL), bel.str(ctx));
                            debug_cell(it->second->name, bel);
                        } else {
                            if (std::regex_match(pinlines[iter], match_pinloc, iobelre)) {
                                // may be it's IOx#[AB] style?
                                Loc loc = getLoc(match_pinloc, ctx->getGridDimX(), ctx->getGridDimY());
                                BelId bel = ctx->getBelByLocation(loc);
                                if (bel == BelId()) {
                                    log_error("Pin %s not found (TRBL style). \n", pinlines[iter].c_str());
                                }
                                it->second->setAttr(IdString(ID_BEL), std::string(ctx->nameOfBel(bel)));
                                debug_cell(it->second->name, ctx->getBelName(bel));
                            } else {
                                log_error("Pin %s not found (pin# style)\n", pinname.c_str(ctx));
                            }
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
                if (!adc_ios.empty()) {
                    log_info("ADC iobufs:\n");
                    for (auto &bus_io : adc_ios) {
                        log_info("  bus %s\n", bus_io.c_str(ctx));
                    }
                }
            }
            if (!adc_ios.empty()) {
                for (auto &cell : ctx->cells) {
                    auto &ci = *cell.second;

                    if (is_adc(&ci)) {
                        int idx = 0;
                        for (auto &bus_io : adc_ios) {
                            ci.setAttr(ctx->idf("ADC_IO_%d", idx), bus_io.str(ctx));
                            ++idx;
                        }
                    }
                }
            }
            return true;
        } catch (log_execution_error_exception) {
            return false;
        }
    }
};

static void add_sip_constraints(Context *ctx, const Extra_package_data_POD *extra)
{
    for (auto cst : extra->cst) {
        auto it = ctx->cells.find(IdString(cst.net));
        if (it == ctx->cells.end()) {
            log_info("Cell %s not found\n", IdString(cst.net).c_str(ctx));
            continue;
        }
        Loc loc = Loc(cst.col, cst.row, cst.bel);
        BelId bel = ctx->getBelByLocation(loc);
        if (bel == BelId()) {
            log_error("Pin not found.\n");
        }
        it->second->setAttr(IdString(ID_BEL), std::string(ctx->nameOfBel(bel)));

        if (cst.iostd > 0) {
            std::string attr = "&IO_TYPE=";
            attr += IdString(cst.iostd).c_str(ctx);
            boost::algorithm::to_upper(attr);
            it->second->setAttr(ctx->id(attr), 1);
        }
    }
}

bool gowin_apply_constraints(Context *ctx, std::istream &in)
{
    // implicit constraints from SiP pins
    if (!ctx->package_info->extra_data.is_null()) {
        const Extra_package_data_POD *extra =
                reinterpret_cast<const Extra_package_data_POD *>(ctx->package_info->extra_data.get());
        add_sip_constraints(ctx, extra);
    }

    GowinCstReader reader(ctx, in);
    return reader.run();
}

NEXTPNR_NAMESPACE_END
