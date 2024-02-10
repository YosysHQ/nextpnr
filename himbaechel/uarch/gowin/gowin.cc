#include <regex>

#include "himbaechel_api.h"
#include "himbaechel_helpers.h"
#include "log.h"
#include "nextpnr.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"

#include "cst.h"
#include "globals.h"
#include "gowin.h"
#include "gowin_utils.h"
#include "pack.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct GowinImpl : HimbaechelAPI
{

    ~GowinImpl(){};
    void init_database(Arch *arch) override;
    void init(Context *ctx) override;

    void pack() override;
    void prePlace() override;
    void postPlace() override;
    void preRoute() override;
    void postRoute() override;

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override;

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;

    // wires
    bool checkPipAvail(PipId pip) const override;

  private:
    HimbaechelHelpers h;
    GowinUtils gwu;

    IdString chip;
    IdString partno;

    std::set<BelId> inactive_bels;

    // Validity checking
    struct GowinCellInfo
    {
        const NetInfo *lut_f = nullptr;
        const NetInfo *ff_d = nullptr, *ff_ce = nullptr, *ff_clk = nullptr, *ff_lsr = nullptr;
        const NetInfo *alu_sum = nullptr;
    };
    std::vector<GowinCellInfo> fast_cell_info;
    void assign_cell_info();

    // bel placement validation
    bool slice_valid(int x, int y, int z) const;
};

struct GowinArch : HimbaechelArch
{
    GowinArch() : HimbaechelArch("gowin"){};

    bool match_device(const std::string &device) override { return device.size() > 2 && device.substr(0, 2) == "GW"; }

    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<GowinImpl>();
    }
} gowinrArch;

void GowinImpl::init_database(Arch *arch)
{
    init_uarch_constids(arch);
    const ArchArgs &args = arch->args;
    std::string family;
    if (args.options.count("family")) {
        family = args.options.at("family");
    } else {
        bool GW2 = args.device.rfind("GW2A", 0) == 0;
        if (GW2) {
            log_error("For the GW2A series you need to specify -vopt family=GW2A-18 or -vopt family=GW2A-18C\n");
        } else {
            std::regex devicere = std::regex("GW1N([SZ]?)[A-Z]*-(LV|UV|UX)([0-9])(C?).*");
            std::smatch match;
            if (!std::regex_match(args.device, match, devicere)) {
                log_error("Invalid device %s\n", args.device.c_str());
            }
            family = stringf("GW1N%s-%s", match[1].str().c_str(), match[3].str().c_str());
            if (family.rfind("GW1N-9", 0) == 0) {
                log_error("For the GW1N-9 series you need to specify -vopt family=GW1N-9 or -vopt family=GW1N-9C\n");
            }
        }
    }

    arch->load_chipdb(stringf("gowin/chipdb-%s.bin", family.c_str()));

    // These fields go in the header of the output JSON file and can help
    // gowin_pack support different architectures
    arch->settings[arch->id("packer.arch")] = std::string("himbaechel/gowin");
    arch->settings[arch->id("packer.chipdb")] = family;

    chip = arch->id(family);
    std::string pn = args.device;
    partno = arch->id(pn);
    arch->settings[arch->id("packer.partno")] = pn;
}

void GowinImpl::init(Context *ctx)
{
    h.init(ctx);
    HimbaechelAPI::init(ctx);

    gwu.init(ctx);

    const ArchArgs &args = ctx->getArchArgs();

    // package and speed class
    std::regex speedre = std::regex("(.*)(C[0-9]/I[0-9])$");
    std::smatch match;

    IdString spd;
    IdString package_idx;
    std::string pn = args.device;
    if (std::regex_match(pn, match, speedre)) {
        package_idx = ctx->id(match[1]);
        spd = ctx->id(match[2]);
    } else {
        if (pn.length() > 2 && pn.compare(pn.length() - 2, 2, "ES")) {
            package_idx = ctx->id(pn.substr(pn.length() - 2));
            spd = ctx->id("ES");
        }
    }

    // log_info("packages:%ld\n", ctx->chip_info->packages.ssize());
    for (int i = 0; i < ctx->chip_info->packages.ssize(); ++i) {
        if (IdString(ctx->chip_info->packages[i].name) == package_idx) {
            // log_info("i:%d %s\n", i, package_idx.c_str(ctx));
            ctx->package_info = &ctx->chip_info->packages[i];
            break;
        }
    }
    if (ctx->package_info == nullptr) {
        log_error("No package for partnumber %s\n", partno.c_str(ctx));
    }

    // constraints
    if (args.options.count("cst")) {
        ctx->settings[ctx->id("cst.filename")] = args.options.at("cst");
    }
}

// We do not allow the use of global wires that bypass a special router.
bool GowinImpl::checkPipAvail(PipId pip) const { return !gwu.is_global_pip(pip); }

void GowinImpl::pack()
{
    if (ctx->settings.count(ctx->id("cst.filename"))) {
        std::string filename = ctx->settings[ctx->id("cst.filename")].as_string();
        std::ifstream in(filename);
        if (!in) {
            log_error("failed to open CST file '%s'\n", filename.c_str());
        }
        if (!gowin_apply_constraints(ctx, in)) {
            log_error("failed to parse CST file '%s'\n", filename.c_str());
        }
    }
    gowin_pack(ctx);
}

void GowinImpl::prePlace() { assign_cell_info(); }
void GowinImpl::postPlace()
{
    if (ctx->debug) {
        log_info("================== Final Placement ===================\n");
        for (auto &cell : ctx->cells) {
            auto ci = cell.second.get();
            if (ci->bel != BelId()) {
                log_info("%s: %s\n", ctx->nameOfBel(ci->bel), ctx->nameOf(ci));
            } else {
                log_info("unknown: %s\n", ctx->nameOf(ci));
            }
        }
        log_break();
    }
}

void GowinImpl::preRoute() { gowin_route_globals(ctx); }

void GowinImpl::postRoute()
{
    std::set<IdString> visited_hclk_users;

    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();
        if (ci->type.in(id_IOLOGICI, id_IOLOGICO, id_IOLOGIC) ||
            ((is_iologici(ci) || is_iologico(ci)) && !ci->type.in(id_ODDR, id_ODDRC, id_IDDR, id_IDDRC))) {
            if (visited_hclk_users.find(ci->name) == visited_hclk_users.end()) {
                // mark FCLK<-HCLK connections
                const NetInfo *h_net = ci->getPort(id_FCLK);
                if (h_net) {
                    for (auto &user : h_net->users) {
                        if (user.port != id_FCLK) {
                            continue;
                        }
                        user.cell->setAttr(id_IOLOGIC_FCLK, Property("UNKNOWN"));
                        visited_hclk_users.insert(user.cell->name);
                        // XXX Based on the implementation, perhaps a function
                        // is needed to get Pip from a Wire
                        PipId up_pip = h_net->wires.at(ctx->getNetinfoSinkWire(h_net, user, 0)).pip;
                        IdString up_wire_name = ctx->getWireName(ctx->getPipSrcWire(up_pip))[1];
                        if (up_wire_name.in(id_HCLK_OUT0, id_HCLK_OUT1, id_HCLK_OUT2, id_HCLK_OUT3)) {
                            user.cell->setAttr(id_IOLOGIC_FCLK, Property(up_wire_name.str(ctx)));
                            if (ctx->debug) {
                                log_info("set IOLOGIC_FCLK to %s\n", up_wire_name.c_str(ctx));
                            }
                        }
                        if (ctx->debug) {
                            log_info("HCLK user cell:%s, port:%s, wire:%s, pip:%s, up wire:%s\n",
                                     ctx->nameOf(user.cell), user.port.c_str(ctx),
                                     ctx->nameOfWire(ctx->getNetinfoSinkWire(h_net, user, 0)), ctx->nameOfPip(up_pip),
                                     ctx->nameOfWire(ctx->getPipSrcWire(up_pip)));
                        }
                    }
                }
            }
        }
    }
}

bool GowinImpl::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    Loc l = ctx->getBelLocation(bel);
    if (!ctx->getBoundBelCell(bel)) {
        return true;
    }
    IdString bel_type = ctx->getBelType(bel);
    switch (bel_type.hash()) {
    case ID_LUT4: /* fall-through */
    case ID_DFF:
        return slice_valid(l.x, l.y, l.z / 2);
    case ID_ALU:
        return slice_valid(l.x, l.y, l.z - BelZ::ALU0_Z);
    case ID_RAM16SDP4:
        // only slices 4 and 5 are critical for RAM
        return slice_valid(l.x, l.y, l.z - BelZ::RAMW_Z + 5) && slice_valid(l.x, l.y, l.z - BelZ::RAMW_Z + 4);
    }
    return true;
}

// Bel bucket functions
IdString GowinImpl::getBelBucketForCellType(IdString cell_type) const
{
    if (cell_type.in(id_IBUF, id_OBUF)) {
        return id_IOB;
    }
    if (type_is_lut(cell_type)) {
        return id_LUT4;
    }
    if (type_is_dff(cell_type)) {
        return id_DFF;
    }
    if (type_is_ssram(cell_type)) {
        return id_RAM16SDP4;
    }
    if (type_is_iologici(cell_type)) {
        return id_IOLOGICI;
    }
    if (type_is_iologico(cell_type)) {
        return id_IOLOGICO;
    }
    if (type_is_bsram(cell_type)) {
        return id_BSRAM;
    }
    if (cell_type == id_GOWIN_GND) {
        return id_GND;
    }
    if (cell_type == id_GOWIN_VCC) {
        return id_VCC;
    }
    return cell_type;
}

bool GowinImpl::isValidBelForCellType(IdString cell_type, BelId bel) const
{
    IdString bel_type = ctx->getBelType(bel);
    if (bel_type == id_IOB) {
        return cell_type.in(id_IBUF, id_OBUF);
    }
    if (bel_type == id_LUT4) {
        return type_is_lut(cell_type);
    }
    if (bel_type == id_DFF) {
        return type_is_dff(cell_type);
    }
    if (bel_type == id_RAM16SDP4) {
        return type_is_ssram(cell_type);
    }
    if (bel_type == id_IOLOGICI) {
        return type_is_iologici(cell_type);
    }
    if (bel_type == id_IOLOGICO) {
        return type_is_iologico(cell_type);
    }
    if (bel_type == id_BSRAM) {
        return type_is_bsram(cell_type);
    }
    if (bel_type == id_GND) {
        return cell_type == id_GOWIN_GND;
    }
    if (bel_type == id_VCC) {
        return cell_type == id_GOWIN_VCC;
    }
    return (bel_type == cell_type);
}

void GowinImpl::assign_cell_info()
{
    fast_cell_info.resize(ctx->cells.size());
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        auto &fc = fast_cell_info.at(ci->flat_index);
        if (is_lut(ci)) {
            fc.lut_f = ci->getPort(id_F);
            continue;
        }
        if (is_dff(ci)) {
            fc.ff_d = ci->getPort(id_D);
            fc.ff_clk = ci->getPort(id_CLK);
            fc.ff_ce = ci->getPort(id_CE);
            for (IdString port : {id_SET, id_RESET, id_PRESET, id_CLEAR}) {
                fc.ff_lsr = ci->getPort(port);
                if (fc.ff_lsr != nullptr) {
                    break;
                }
            }
            continue;
        }
        if (is_alu(ci)) {
            fc.alu_sum = ci->getPort(id_SUM);
            continue;
        }
    }
}

// DFFs must be same type or compatible
inline bool incompatible_ffs(const CellInfo *ff, const CellInfo *adj_ff)
{
    return ff->type != adj_ff->type &&
           ((ff->type == id_DFFS && adj_ff->type != id_DFFR) || (ff->type == id_DFFR && adj_ff->type != id_DFFS) ||
            (ff->type == id_DFFSE && adj_ff->type != id_DFFRE) || (ff->type == id_DFFRE && adj_ff->type != id_DFFSE) ||
            (ff->type == id_DFFP && adj_ff->type != id_DFFC) || (ff->type == id_DFFC && adj_ff->type != id_DFFP) ||
            (ff->type == id_DFFPE && adj_ff->type != id_DFFCE) || (ff->type == id_DFFCE && adj_ff->type != id_DFFPE) ||
            (ff->type == id_DFFNS && adj_ff->type != id_DFFNR) || (ff->type == id_DFFNR && adj_ff->type != id_DFFNS) ||
            (ff->type == id_DFFNSE && adj_ff->type != id_DFFNRE) ||
            (ff->type == id_DFFNRE && adj_ff->type != id_DFFNSE) ||
            (ff->type == id_DFFNP && adj_ff->type != id_DFFNC) || (ff->type == id_DFFNC && adj_ff->type != id_DFFNP) ||
            (ff->type == id_DFFNPE && adj_ff->type != id_DFFNCE) ||
            (ff->type == id_DFFNCE && adj_ff->type != id_DFFNPE));
}

// placement validation
bool GowinImpl::slice_valid(int x, int y, int z) const
{
    const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
    const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
    const CellInfo *alu = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z + BelZ::ALU0_Z)));
    const CellInfo *ramw =
            (z == 4 || z == 5) ? ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, BelZ::RAMW_Z))) : nullptr;

    if (alu && lut) {
        return false;
    }

    if (ramw) {
        if (alu || ff || lut) {
            return false;
        }
        return true;
    }

    // check for ALU/LUT in the adjacent cell
    int adj_lut_z = (1 - (z & 1) * 2 + z) * 2;
    int adj_alu_z = adj_lut_z / 2 + BelZ::ALU0_Z;
    const CellInfo *adj_lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_lut_z)));
    const CellInfo *adj_ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_lut_z + 1)));
    const CellInfo *adj_alu = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_alu_z)));

    if ((alu && (adj_lut || (adj_ff && !adj_alu))) || ((lut || (ff && !alu)) && adj_alu)) {
        return false;
    }

    // if there is DFF it must be connected to this LUT or ALU
    if (ff) {
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        if (lut) {
            const auto &lut_data = fast_cell_info.at(lut->flat_index);
            if (ff_data.ff_d != lut_data.lut_f) {
                return false;
            }
        }
        if (alu) {
            const auto &alu_data = fast_cell_info.at(alu->flat_index);
            if (ff_data.ff_d != alu_data.alu_sum) {
                return false;
            }
        }
        if (adj_ff) {
            if (incompatible_ffs(ff, adj_ff)) {
                return false;
            }

            // CE, LSR and CLK must match
            const auto &adj_ff_data = fast_cell_info.at(adj_ff->flat_index);
            if (adj_ff_data.ff_lsr != ff_data.ff_lsr) {
                return false;
            }
            if (adj_ff_data.ff_clk != ff_data.ff_clk) {
                return false;
            }
            if (adj_ff_data.ff_ce != ff_data.ff_ce) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

NEXTPNR_NAMESPACE_END
