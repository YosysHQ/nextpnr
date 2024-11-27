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

    ~GowinImpl() {};
    void init_database(Arch *arch) override;
    void init(Context *ctx) override;

    void pack() override;
    void prePlace() override;
    void postPlace() override;
    void preRoute() override;
    void postRoute() override;

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override;
    void notifyBelChange(BelId bel, CellInfo *cell) override;

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override;

    // wires
    bool checkPipAvail(PipId pip) const override;

    // Cluster
    bool isClusterStrict(const CellInfo *cell) const { return true; }
    bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                             std::vector<std::pair<CellInfo *, BelId>> &placement) const;

  private:
    HimbaechelHelpers h;
    GowinUtils gwu;

    IdString chip;
    IdString partno;

    std::set<BelId> inactive_bels;

    // Validity checking
    struct GowinCellInfo
    {
        // slice info
        const NetInfo *lut_f = nullptr;
        const NetInfo *ff_d = nullptr, *ff_ce = nullptr, *ff_clk = nullptr, *ff_lsr = nullptr;
        const NetInfo *alu_sum = nullptr;
        // dsp info
        const NetInfo *dsp_asign = nullptr, *dsp_bsign = nullptr, *dsp_asel = nullptr, *dsp_bsel = nullptr,
                      *dsp_ce = nullptr, *dsp_clk = nullptr, *dsp_reset = nullptr;
        bool dsp_soa_reg;
    };
    std::vector<GowinCellInfo> fast_cell_info;
    void assign_cell_info();

    // If there is an unused LUT adjacent to FF, use it
    void create_passthrough_luts(void);

    // Remember HCLK sections that have been reserved to route HCLK signals
    std::set<BelId> routing_reserved_hclk_sections;

    // dsp control nets
    // Each DSP and each macro has a small set of control wires that are
    // allocated to internal primitives as needed. It is assumed that most
    // primitives use the same signals for CE, CLK and especially RESET, so
    // these wires are few and need to be controlled.
    struct dsp_net_counters
    {
        dict<IdString, int> ce;
        dict<IdString, int> clk;
        dict<IdString, int> reset;
    };
    dict<BelId, dsp_net_counters> dsp_net_cnt;
    dict<BelId, CellInfo *> dsp_bel2cell; // Remember the connection with cells
                                          // since this information is already lost during unbinding
    void adjust_dsp_pin_mapping(void);

    // Place explicityl constrained or implicitly constrained (by IOLOGIC) CLKDIV and CLKDIV2 cells
    // to avoid routing conflicts and maximize utilization
    void place_constrained_hclk_cells();

    // bel placement validation
    bool slice_valid(int x, int y, int z) const;
    bool dsp_valid(Loc l, IdString bel_type, bool explain_invalid) const;
    bool hclk_valid(BelId bel, IdString bel_type) const;
};

struct GowinArch : HimbaechelArch
{
    GowinArch() : HimbaechelArch("gowin") {};

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
            log_error("For the GW2A series you need to specify --vopt family=GW2A-18 or --vopt family=GW2A-18C\n");
        } else {
            std::regex devicere = std::regex("GW1N([SZ]?)[A-Z]*-(LV|UV|UX)([0-9])(C?).*");
            std::smatch match;
            if (!std::regex_match(args.device, match, devicere)) {
                log_error("Invalid device %s\n", args.device.c_str());
            }
            family = stringf("GW1N%s-%s", match[1].str().c_str(), match[3].str().c_str());
            if (family.rfind("GW1N-9", 0) == 0) {
                log_error("For the GW1N-9 series you need to specify --vopt family=GW1N-9 or --vopt family=GW1N-9C\n");
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
        ctx->set_speed_grade(match[2]);
    } else {
        if (pn.length() > 2 && pn.compare(pn.length() - 2, 2, "ES")) {
            package_idx = ctx->id(pn.substr(pn.length() - 2));
            spd = ctx->id("ES");
            ctx->set_speed_grade("ES");
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
bool GowinImpl::checkPipAvail(PipId pip) const
{
    return (ctx->getWireConstantValue(ctx->getPipSrcWire(pip)) != IdString()) || (!gwu.is_global_pip(pip));
}

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

// One DSP macro, in a rough approximation, consists of 5 large operating
// blocks (pre-adders, multipliers and alu), at almost every input (blocks
// usually have two of them) you can turn on registers, in addition, there are
// registers on a dedicated operand shift line between DSP and registers at
// the outputs. As we see, the number of registers is large, but the DSP has
// only four inputs for each of the CE, CLK and RESET signals, and here we tell
// gowin_pack which version of each signal is used by which block.
// We also indicate to the router which Bel's pin to use.
void GowinImpl::adjust_dsp_pin_mapping(void)
{
    for (auto b2c : dsp_bel2cell) {
        BelId bel = b2c.first;
        Loc loc = ctx->getBelLocation(bel);
        CellInfo *ci = b2c.second;
        const auto dsp_data = fast_cell_info.at(ci->flat_index);

        auto set_cell_bel_pin = [&](dict<IdString, int> nets, IdString pin, IdString net_name, const char *fmt,
                                    const char *fmt_double = nullptr) {
            int i = 0;
            for (auto net_cnt : nets) {
                if (net_cnt.first == net_name) {
                    break;
                }
                ++i;
            }
            ci->cell_bel_pins.at(pin).clear();
            if (fmt_double == nullptr) {
                ci->cell_bel_pins.at(pin).push_back(ctx->idf(fmt, i));
            } else {
                ci->cell_bel_pins.at(pin).push_back(ctx->idf(fmt_double, i, 0));
                ci->cell_bel_pins.at(pin).push_back(ctx->idf(fmt_double, i, 1));
            }
            ci->setAttr(pin, i);
        };

        if (dsp_data.dsp_reset != nullptr) {
            BelId dsp = ctx->getBelByLocation(Loc(loc.x, loc.y, BelZ::DSP_Z));
            set_cell_bel_pin(dsp_net_cnt.at(dsp).reset, id_RESET, dsp_data.dsp_reset->name, "RESET%d",
                             ci->type == id_MULT36X36 ? "RESET%d%d" : nullptr);
        }
        if (dsp_data.dsp_ce != nullptr) {
            BelId dsp = ctx->getBelByLocation(Loc(loc.x, loc.y, gwu.get_dsp_macro(loc.z)));
            set_cell_bel_pin(dsp_net_cnt.at(dsp).ce, id_CE, dsp_data.dsp_ce->name, "CE%d",
                             ci->type == id_MULT36X36 ? "CE%d%d" : nullptr);
        }
        if (dsp_data.dsp_clk != nullptr) {
            BelId dsp = ctx->getBelByLocation(Loc(loc.x, loc.y, gwu.get_dsp_macro(loc.z)));
            set_cell_bel_pin(dsp_net_cnt.at(dsp).clk, id_CLK, dsp_data.dsp_clk->name, "CLK%d",
                             ci->type == id_MULT36X36 ? "CLK%d%d" : nullptr);
        }
    }
}

/*
   Each HCLK section can serve one of three purposes:
       1. A simple routing path to IOLOGIC FCLK
       2. CLKDIV2
       3. CLKDIV (only one section at any time)

   Our task is to distribute HCLK signal providers to sections in a way that maximizes utilization while
   enforcing user constraints on CLKDIV placement. We achieve this by solving two bipartite matchings:
   - The first determines the best HCLK to place a CLKDIV within the established graph. This is then refined
       to determine what section to assign the CLKDIV to based on what IOLOGIC it connects to
   - The second determines which HCLK sections to use as CLKDIV2 or to reserve for routing.
*/
void GowinImpl::place_constrained_hclk_cells()
{
    log_info("Running custom HCLK placer...\n");
    std::map<IdStringList, IdString> constrained_clkdivs;
    std::map<BelId, std::set<std::pair<IdString, int>>> bel_cell_map;
    std::vector<std::pair<IdString, int>> alias_cells;
    std::map<std::pair<IdString, int>, BelId> final_placement;

    std::set<IdString> seen_hclk_users;
    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();

        if (is_clkdiv(ci) && ci->attrs.count(id_BEL)) {
            BelId constrained_bel = ctx->getBelByName(IdStringList::parse(ctx, ci->attrs.at(id_BEL).as_string()));
            NPNR_ASSERT(constrained_bel != BelId() && ctx->getBelType(constrained_bel) == id_CLKDIV);
            auto hclk_id_loc = gwu.get_hclk_id(constrained_bel);
            constrained_clkdivs[hclk_id_loc] = ci->name;
        }

        if ((seen_hclk_users.find(ci->name) != seen_hclk_users.end()))
            continue;

        if (((is_iologici(ci) || is_iologico(ci)) &&
             !ci->type.in(id_ODDR, id_ODDRC, id_IDDR, id_IDDRC, id_IOLOGICI_EMPTY))) {
            NetInfo *hclk_net = ci->getPort(id_FCLK);
            if (hclk_net)
                continue;
            CellInfo *hclk_driver = hclk_net->driver.cell;
            if (!hclk_driver)
                continue;
            if (chip.str(ctx) == "GW1N-9C" && hclk_driver->type != id_CLKDIV2) {
                // CLKDIV doesn't seem to connect directly to FCLK on this device, and routing is guaranteed to succeed.
                continue;
            }

            int alias_count = 0;
            std::set<std::set<BelId>> seen_options;
            for (auto user : hclk_net->users) {
                std::vector<BelId> bel_candidates;
                std::set<BelId> these_options;

                if (!(user.port == id_FCLK && (is_iologici(user.cell) || is_iologico(user.cell)) &&
                      !user.cell->type.in(id_ODDR, id_ODDRC, id_IDDR, id_IDDRC)))
                    continue;
                if (seen_hclk_users.find(user.cell->name) != seen_hclk_users.end())
                    continue;
                seen_hclk_users.insert(user.cell->name);

                if (ctx->debug) {
                    log_info("Custom HCLK Placer: Found HCLK user: %s\n", user.cell->name.c_str(ctx));
                }

                gwu.find_connected_bels(user.cell, id_FCLK, id_CLKDIV2, id_CLKOUT, 16, bel_candidates);
                these_options.insert(bel_candidates.begin(), bel_candidates.end());

                if (seen_options.find(these_options) != seen_options.end())
                    continue;
                seen_options.insert(these_options);

                // When an HCLK signal is routed to different (and disconnected) FCLKs, we treat each new
                // HCLK-FCLK connection as a pseudo-HCLK cell since it must also be assigned an HCLK section
                auto alias_index = std::pair<IdString, int>(hclk_driver->name, alias_count);
                alias_cells.push_back(alias_index);
                alias_count++;

                for (auto option : these_options) {
                    bel_cell_map[option].insert(alias_index);
                }
            }
        }
    }

    // First matching. We use the upper CLKDIV2 as the ID for an HCLK
    std::map<IdStringList, std::set<IdString>> clkdiv_graph;
    for (auto bel_cell_candidates : bel_cell_map) {
        auto bel = bel_cell_candidates.first;
        auto hclk_id_loc = gwu.get_hclk_id(bel);
        if (constrained_clkdivs.find(hclk_id_loc) != constrained_clkdivs.end()) {
            continue;
        }
        for (auto candidate : bel_cell_candidates.second) {
            auto ci = ctx->cells.at(candidate.first).get();
            if ((ci->type != id_CLKDIV) || ci->attrs.count(id_BEL)) {
                continue;
            }
            clkdiv_graph[hclk_id_loc].insert(candidate.first);
        }
    }

    if (ctx->debug) {
        log_info("<-----CUSTOM HCLK PLACER: Constrained CLKDIVs----->\n");
        for (auto match_pair : constrained_clkdivs) {
            log_info("%s cell <-----> CLKDIV at HCLK %s\n", match_pair.second.c_str(ctx), match_pair.second.c_str(ctx));
        }
        log("\n");
    }

    auto matching = gwu.find_maximum_bipartite_matching<IdStringList, IdString>(
            clkdiv_graph); // these will serve as constraints
    constrained_clkdivs.insert(matching.begin(), matching.end());

    if (ctx->debug) {
        log_info("<-----CUSTOM HCLK PLACER: First Matching(CLKDIV) Results----->\n");
        for (auto match_pair : matching) {
            log_info("%s cell <-----> CLKDIV at HCLK %s\n", match_pair.second.c_str(ctx), match_pair.second.c_str(ctx));
        }
        log("\n");
    }

    // Refine matching to HCLK section, based on what connections actually exist
    std::map<IdString, std::pair<IdString, int>> true_clkdivs;
    std::set<BelId> used_bels;
    for (auto constr_pair : constrained_clkdivs) {
        BelId option0 = ctx->getBelByName(constr_pair.first);
        BelId option1 = gwu.get_other_hclk_clkdiv2(option0);

        // On the GW1N-9 devices, only the lower CLKDIV can be fed by a CLKDIV2
        std::vector<BelId> options = {option1, option0};
        if (chip.str(ctx) == "GW1N-9C") {
            auto ci = ctx->cells.at(constr_pair.second).get();
            for (auto cluster_child_cell : ci->constr_children)
                if (cluster_child_cell->type == id_CLKDIV2 && options.back() == option0) {
                    options.pop_back();
                    break;
                }
        }

        bool placed = false;
        for (auto option : options) {
            if (placed || (used_bels.find(option) != used_bels.end()))
                continue;
            for (auto option_cell : bel_cell_map[option]) {
                if ((option_cell.first != constr_pair.second) ||
                    (true_clkdivs.find(option_cell.first) != true_clkdivs.end()))
                    continue;
                final_placement[option_cell] = option;
                true_clkdivs[option_cell.first] = option_cell;
                used_bels.insert(option);
                placed = true;
                break;
            }
        }
        // This must be a constrained CLKDIV that either does not serve IOLOGIC Or
        // does not have a direct (HCLK-FCLK) connection IOLOGIC it serves
        // We create a new alias to represent this
        if (!placed) {
            auto new_alias = std::pair<IdString, int>(constr_pair.second, -1);
            for (auto option : options)
                bel_cell_map[option].insert(new_alias);
            alias_cells.push_back(new_alias);
            true_clkdivs[constr_pair.second] = new_alias;
        }
    }

    // Second Matching for CLKDIV2 and routing reservation
    std::map<IdStringList, std::set<std::pair<IdString, int>>> full_hclk_graph;
    for (auto bel_cell_candidates : bel_cell_map) {
        auto bel = bel_cell_candidates.first;
        auto bel_name = ctx->getBelName(bel);
        if (!used_bels.count(bel)) {
            for (auto candidate : bel_cell_candidates.second) {
                if (((candidate.second == -1) || (!true_clkdivs.count(candidate.first)) ||
                     !(true_clkdivs[candidate.first] == candidate))) {
                    full_hclk_graph[bel_name].insert(candidate);
                }
            }
        }
    }

    auto full_matching = gwu.find_maximum_bipartite_matching(full_hclk_graph);
    for (auto belname_cellalias : full_matching) {
        auto bel = ctx->getBelByName(belname_cellalias.first);
        NPNR_ASSERT(!used_bels.count(bel));
        final_placement[belname_cellalias.second] = bel;
    }

    if (ctx->debug) {
        log_info("<-----CUSTOM HCLK PLACER: Second Matching(CLKDIV2 and Routing) Results------>\n");
        for (auto match_pair : full_matching) {
            auto alias = match_pair.second;
            auto bel = match_pair.first;
            auto cell_type = ctx->cells.at(alias.first).get()->type;
            log_info("%s cell %s Alias %d <-----> HCLK Section at %s\n", cell_type.c_str(ctx), alias.first.c_str(ctx),
                     alias.second, bel.str(ctx).c_str());
        }
        log("\n");
    }

    for (auto cell_alias : alias_cells) {
        auto ci = ctx->cells.at(cell_alias.first).get();

        if (final_placement.find(cell_alias) == final_placement.end() && ctx->debug)
            if (ci->type == id_CLKDIV2 || ci->type == id_CLKDIV)
                log_info("Custom HCLK Placer: Unable to place HCLK cell %s; no BELs available to implement cell type "
                         "%s\n",
                         ci->name.c_str(ctx), ci->type.c_str(ctx));
            else
                log_info("Custom HCLK Placer: Unable to guarantee route for HCLK signal from %s to IOLOGIC\n",
                         ci->name.c_str(ctx));

        else {
            auto placement = final_placement[cell_alias];
            if (ctx->debug)
                log_info("Custom HCLK Placer: Placing %s Alias %d at %s\n", cell_alias.first.c_str(ctx),
                         cell_alias.second, ctx->nameOfBel(placement));
            if (ci->type == id_CLKDIV2)
                ctx->bindBel(placement, ci, STRENGTH_LOCKED);

            else if ((ci->type == id_CLKDIV) && (true_clkdivs[cell_alias.first] == cell_alias)) {
                NetInfo *in = ci->getPort(id_HCLKIN);
                if (in && in->driver.cell->type == id_CLKDIV2) {
                    ctx->bindBel(placement, in->driver.cell, STRENGTH_LOCKED);
                }
                auto clkdiv_bel = gwu.get_clkdiv_for_clkdiv2(placement);
                ctx->bindBel(clkdiv_bel, ci, STRENGTH_LOCKED);
            } else {
                if (ctx->debug)
                    log_info("Custom HCLK Placer: Reserving HCLK %s to route clock from %s\n",
                             ctx->nameOfBel(placement), ci->name.c_str(ctx));
                routing_reserved_hclk_sections.insert(placement);
            }
        }
        if (ci->attrs.count(id_BEL))
            ci->unsetAttr(id_BEL);
    }
}

void GowinImpl::prePlace()
{
    place_constrained_hclk_cells();
    assign_cell_info();
}

void GowinImpl::postPlace()
{
    gwu.has_SP32();
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

    // adjust cell pin to bel pin mapping for DSP cells (CE, CLK and RESET pins)
    adjust_dsp_pin_mapping();
    create_passthrough_luts();
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
    IdString bel_type = ctx->getBelType(bel);
    if (!ctx->getBoundBelCell(bel)) {
        return true;
    }
    switch (bel_type.hash()) {
    case ID_LUT4: /* fall-through */
    case ID_DFF:
        return slice_valid(l.x, l.y, l.z / 2);
    case ID_ALU:
        return slice_valid(l.x, l.y, l.z - BelZ::ALU0_Z);
    case ID_RAM16SDP4:
        return slice_valid(l.x, l.y, 0);
    case ID_MUX2_LUT5:
        return slice_valid(l.x, l.y, (l.z - BelZ::MUX20_Z) / 2);
    case ID_MUX2_LUT6:
        return slice_valid(l.x, l.y, (l.z - BelZ::MUX21_Z) / 2 + 1);
    case ID_MUX2_LUT7:
        return slice_valid(l.x, l.y, 3);
    case ID_MUX2_LUT8:
        return slice_valid(l.x, l.y, 7);
    case ID_PADD9:           /* fall-through */
    case ID_PADD18:          /* fall-through */
    case ID_MULT9X9:         /* fall-through */
    case ID_MULT18X18:       /* fall-through */
    case ID_MULTADDALU18X18: /* fall-through */
    case ID_MULTALU18X18:    /* fall-through */
    case ID_MULTALU36X18:    /* fall-through */
    case ID_MULT36X36:       /* fall-through */
    case ID_ALU54D:
        return dsp_valid(l, bel_type, explain_invalid);
    case ID_CLKDIV2: /* fall-through */
    case ID_CLKDIV:
        return hclk_valid(bel, bel_type);
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
    if (cell_type == id_DUMMY_CELL) {
        return true;
    }

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
        auto get_net = [&](IdString port_id) {
            NetInfo *ni = ci->getPort(port_id);
            if (ni != nullptr && ni->driver.cell == nullptr) {
                ni = nullptr;
            }
            return ni;
        };
        if (is_dsp(ci)) {
            fc.dsp_reset = get_net(id_RESET);
            fc.dsp_clk = get_net(id_CLK);
            fc.dsp_ce = get_net(id_CE);
            fc.dsp_asign = get_net(id_ASIGN);
            fc.dsp_bsign = get_net(id_BSIGN);
            fc.dsp_asel = get_net(id_ASEL);
            fc.dsp_bsel = get_net(id_BSEL);
            fc.dsp_soa_reg = ci->params.count(id_SOA_REG) && ci->params.at(id_SOA_REG).as_int64() == 1;
        }
    }
}

// If there is an unused LUT next to the DFF, use its inputs for the D input
void GowinImpl::create_passthrough_luts(void)
{
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_dff(ci)) {
            Loc loc = ctx->getBelLocation(ci->bel);
            BelId lut_bel = ctx->getBelByLocation(Loc(loc.x, loc.y, loc.z - 1));
            CellInfo *lut = ctx->getBoundBelCell(lut_bel);
            CellInfo *alu = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, loc.z / 2 + BelZ::ALU0_Z)));
            const CellInfo *ramw = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(loc.x, loc.y, BelZ::RAMW_Z)));

            if (!(lut || alu || ramw)) {
                if (ctx->debug) {
                    log_info("Found an unused LUT:%s, ", ctx->nameOfBel(lut_bel));
                }
                // make LUT
                auto lut_cell = gwu.create_cell(gwu.create_aux_name(ci->name, 0, "_passthrough_lut$"), id_LUT4);
                CellInfo *lut = lut_cell.get();
                NetInfo *d_net = ci->getPort(id_D);
                NPNR_ASSERT(d_net != nullptr);

                if (d_net->name == ctx->id("$PACKER_GND") || d_net->name == ctx->id("$PACKER_VCC")) {
                    if (ctx->debug) {
                        log("make a constant %s.\n", d_net->name == ctx->id("$PACKER_VCC") ? "VCC" : "GND");
                    }
                    ci->disconnectPort(id_D);
                    if (d_net->name == ctx->id("$PACKER_GND")) {
                        lut->setParam(id_INIT, 0x0000);
                    } else {
                        lut->setParam(id_INIT, 0xffff);
                    }
                } else {
                    if (ctx->debug) {
                        log("make a pass-through.\n");
                    }
                    IdString lut_input = id_I3;
                    int lut_init = 0xff00;

                    lut->addInput(lut_input);
                    lut->cell_bel_pins[lut_input].clear();
                    lut->cell_bel_pins.at(lut_input).push_back(lut_input);
                    ci->movePortTo(id_D, lut, lut_input);
                    lut->setParam(id_INIT, lut_init);
                }
                lut->addOutput(id_F);
                lut->cell_bel_pins[id_F].clear();
                lut->cell_bel_pins.at(id_F).push_back(id_F);
                ci->connectPorts(id_D, lut, id_F);

                ctx->bindBel(lut_bel, lut, PlaceStrength::STRENGTH_LOCKED);
                new_cells.push_back(std::move(lut_cell));
            }
        }
    }
    for (auto &cell : new_cells) {
        ctx->cells[cell->name] = std::move(cell);
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
bool GowinImpl::dsp_valid(Loc l, IdString bel_type, bool explain_invalid) const
{
    const CellInfo *dsp = ctx->getBoundBelCell(ctx->getBelByLocation(l));
    const auto &dsp_data = fast_cell_info.at(dsp->flat_index);
    // check for shift out register - there is only one for macro
    if (dsp_data.dsp_soa_reg) {
        if (l.z == BelZ::MULT18X18_0_1_Z || l.z == BelZ::MULT18X18_1_1_Z || l.z == BelZ::MULT9X9_0_0_Z ||
            l.z == BelZ::MULT9X9_0_1_Z || l.z == BelZ::MULT9X9_1_0_Z || l.z == BelZ::MULT9X9_1_1_Z) {
            if (explain_invalid) {
                log_nonfatal_error(
                        "It is not possible to place the DSP so that the SOA register is on the macro boundary.\n");
            }
            return false;
        }
    }

    if (bel_type.in(id_MULT9X9, id_PADD9)) {
        int pair_z = gwu.get_dsp_paired_9(l.z);
        const CellInfo *adj_dsp9 = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, pair_z)));
        if (adj_dsp9 != nullptr) {
            const auto &adj_dsp9_data = fast_cell_info.at(adj_dsp9->flat_index);
            if ((dsp_data.dsp_asign != adj_dsp9_data.dsp_asign) || (dsp_data.dsp_bsign != adj_dsp9_data.dsp_bsign) ||
                (dsp_data.dsp_asel != adj_dsp9_data.dsp_asel) || (dsp_data.dsp_bsel != adj_dsp9_data.dsp_bsel) ||
                (dsp_data.dsp_reset != adj_dsp9_data.dsp_reset) || (dsp_data.dsp_ce != adj_dsp9_data.dsp_ce) ||
                (dsp_data.dsp_clk != adj_dsp9_data.dsp_clk)) {
                if (explain_invalid) {
                    log_nonfatal_error("For 9bit primitives the control signals must be same.\n");
                }
                return false;
            }
        }
    }
    // check for control nets "overflow"
    BelId dsp_bel = ctx->getBelByLocation(Loc(l.x, l.y, BelZ::DSP_Z));
    if (dsp_net_cnt.at(dsp_bel).reset.size() > 4) {
        if (explain_invalid) {
            log_nonfatal_error("More than 4 different networks for RESET signals in one DSP are not allowed.\n");
        }
        return false;
    }
    BelId dsp_macro_bel = ctx->getBelByLocation(Loc(l.x, l.y, gwu.get_dsp_macro(l.z)));
    if (dsp_net_cnt.count(dsp_macro_bel)) {
        if (dsp_net_cnt.at(dsp_macro_bel).ce.size() > 4 || dsp_net_cnt.at(dsp_macro_bel).clk.size() > 4) {
            if (explain_invalid) {
                log_nonfatal_error(
                        "More than 4 different networks for CE or CLK signals in one DSP macro are not allowed.\n");
            }
            return false;
        }
    }
    return true;
}

bool GowinImpl::slice_valid(int x, int y, int z) const
{
    const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
    const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
    // There are only 6 ALUs
    const CellInfo *alu = (z < 6) ? ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z + BelZ::ALU0_Z))) : nullptr;
    const CellInfo *ramw = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, BelZ::RAMW_Z)));

    if (alu && lut) {
        return false;
    }

    if (ramw) {
        // FFs in slices 4 and 5 are not allowed
        if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, 4 * 2 + 1))) ||
            ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, 5 * 2 + 1)))) {
            return false;
        }
        // ALU/LUTs in slices 4, 5, 6, 7 are not allowed
        for (int i = 4; i < 8; ++i) {
            if (ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, i * 2)))) {
                return false;
            }
            if (i < 6 && ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, i + BelZ::ALU0_Z)))) {
                return false;
            }
        }
    }

    // check for ALU/LUT in the adjacent cell
    int adj_lut_z = (1 - (z & 1) * 2 + z) * 2;
    int adj_alu_z = adj_lut_z / 2 + BelZ::ALU0_Z;
    const CellInfo *adj_lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_lut_z)));
    const CellInfo *adj_ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_lut_z + 1)));
    const CellInfo *adj_alu = adj_alu_z < (6 + BelZ::ALU0_Z)
                                      ? ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, adj_alu_z)))
                                      : nullptr;

    if ((alu && (adj_lut || (adj_ff && !adj_alu))) || ((lut || (ff && !alu)) && adj_alu)) {
        return false;
    }

    if (ff) {
        static std::vector<int> mux_z = {BelZ::MUX20_Z,     BelZ::MUX21_Z,     BelZ::MUX20_Z + 4,  BelZ::MUX23_Z,
                                         BelZ::MUX20_Z + 8, BelZ::MUX21_Z + 8, BelZ::MUX20_Z + 12, BelZ::MUX27_Z};
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        const NetInfo *src;
        // check implcit LUT(ALU) -> FF connection
        if (lut || alu) {
            if (lut) {
                src = fast_cell_info.at(lut->flat_index).lut_f;
            } else {
                src = fast_cell_info.at(alu->flat_index).alu_sum;
            }
            if (ff_data.ff_d != src) {
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
/*
    Every HCLK section can be used in one of 3 ways:
        1. As a simple routing path to IOLOGIC FCLK
        2. As a CLKDIV2
        3. As a CLKDIV (potentially fed by the CLKDIV2 in its section)

    Here we validate that the placement of cells fits within these 3 use cases, while ensuring that
    we enforce the constraint that only 1 CLKDIV can be used per HCLK (there is only 1 CLKDIV in each
    HCLK but we pretend there are two because doing so makes it easier to enforce the real constraint
    that HCLK signals don't crisscross between HCLK sections even after "transformation" by a CLKDIV
    or CLKDIV2)
*/
bool GowinImpl::hclk_valid(BelId bel, IdString bel_type) const
{
    if (bel_type == id_CLKDIV2) {
        if (routing_reserved_hclk_sections.count(bel))
            return false;
        auto clkdiv_cell = ctx->getBoundBelCell(gwu.get_clkdiv_for_clkdiv2(bel));
        if (clkdiv_cell && ctx->getBoundBelCell(bel)->cluster != clkdiv_cell->name)
            return false;
        return true;
    } else if (bel_type == id_CLKDIV) {
        BelId clkdiv2_bel = gwu.get_clkdiv2_for_clkdiv(bel);
        if (routing_reserved_hclk_sections.count(clkdiv2_bel)) {
            return false;
        }

        auto other_clkdiv_cell = ctx->getBoundBelCell(gwu.get_other_hclk_clkdiv(bel));
        if (other_clkdiv_cell)
            return false;

        auto clkdiv2_bel_cell = ctx->getBoundBelCell(clkdiv2_bel);
        if (clkdiv2_bel_cell && clkdiv2_bel_cell->cluster != ctx->getBoundBelCell(bel)->name)
            return false;

        if (clkdiv2_bel_cell && chip.str(ctx) == "GW1N-9C") {
            // On the GW1N(R)-9C, it appears that only the 'odd' CLKDIV2 is connected to CLKDIV
            Loc loc = ctx->getBelLocation(bel);
            if (loc.z == BelZ::CLKDIV_0_Z || loc.z == BelZ::CLKDIV_2_Z)
                return false;
        }

        return true;
    }
    return false;
}

// Cluster
bool GowinImpl::getClusterPlacement(ClusterId cluster, BelId root_bel,
                                    std::vector<std::pair<CellInfo *, BelId>> &placement) const
{
    CellInfo *root_ci = getClusterRootCell(cluster);
    if (!root_ci->type.in(id_PADD9, id_MULT9X9, id_PADD18, id_MULT18X18, id_MULTALU18X18, id_MULTALU36X18,
                          id_MULTADDALU18X18, id_ALU54D)) {
        return HimbaechelAPI::getClusterPlacement(cluster, root_bel, placement);
    }

    NPNR_ASSERT(root_bel != BelId());
    if (!isValidBelForCellType(root_ci->type, root_bel)) {
        return false;
    }

    IdString bel_type = ctx->getBelType(root_bel);
    // non-chain DSP
    if (root_ci->constr_children.size() == 1 && bel_type.in(id_PADD9, id_MULT9X9)) {
        return HimbaechelAPI::getClusterPlacement(cluster, root_bel, placement);
    }

    placement.clear();
    Loc root_loc = ctx->getBelLocation(root_bel);
    placement.emplace_back(root_ci, root_bel);

    Loc mult_loc = root_loc;
    for (auto child : root_ci->constr_children) {
        Loc child_loc;
        child_loc.y = root_loc.y;
        if (child->type == id_DUMMY_CELL) {
            child_loc.x = mult_loc.x + child->constr_x;
            child_loc.z = mult_loc.z + child->constr_z;
        } else {
            child_loc = gwu.get_dsp_next_in_chain(mult_loc, child->type);
            mult_loc = child_loc;
        }

        BelId child_bel = ctx->getBelByLocation(child_loc);
        if (child_bel == BelId() || !isValidBelForCellType(child->type, child_bel))
            return false;
        placement.emplace_back(child, child_bel);
    }
    return true;
}

void GowinImpl::notifyBelChange(BelId bel, CellInfo *cell)
{
    if (cell != nullptr && !is_dsp(cell)) {
        return;
    }
    if (cell == nullptr && dsp_bel2cell.count(bel) == 0) {
        return;
    }

    // trace DSP control networks
    IdString cell_type = id_DUMMY_CELL;
    if (cell != nullptr) {
        cell_type = cell->type;
    }
    Loc loc = ctx->getBelLocation(bel);
    Loc l = loc;
    l.z = gwu.get_dsp(loc.z);
    BelId dsp = ctx->getBelByLocation(l);
    l.z = gwu.get_dsp_macro(loc.z);
    BelId dsp_macro = ctx->getBelByLocation(l);

    if (cell) {
        const auto &dsp_cell_data = fast_cell_info.at(cell->flat_index);
        if (dsp_cell_data.dsp_reset != nullptr) {
            dsp_net_cnt[dsp].reset[dsp_cell_data.dsp_reset->name]++;
        }
        if (dsp_cell_data.dsp_ce != nullptr) {
            dsp_net_cnt[dsp_macro].ce[dsp_cell_data.dsp_ce->name]++;
        }
        if (dsp_cell_data.dsp_clk != nullptr) {
            dsp_net_cnt[dsp_macro].clk[dsp_cell_data.dsp_clk->name]++;
        }
        dsp_bel2cell[bel] = cell;
    } else {
        const auto &dsp_cell_data = fast_cell_info.at(dsp_bel2cell.at(bel)->flat_index);
        if (dsp_cell_data.dsp_reset != nullptr) {
            dsp_net_cnt.at(dsp).reset.at(dsp_cell_data.dsp_reset->name)--;
        }
        if (dsp_cell_data.dsp_ce != nullptr) {
            dsp_net_cnt.at(dsp_macro).ce.at(dsp_cell_data.dsp_ce->name)--;
        }
        if (dsp_cell_data.dsp_clk != nullptr) {
            dsp_net_cnt.at(dsp_macro).clk.at(dsp_cell_data.dsp_clk->name)--;
        }
        dsp_bel2cell.erase(bel);
    }
}

} // namespace

NEXTPNR_NAMESPACE_END
