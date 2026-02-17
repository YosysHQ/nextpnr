#include <boost/program_options.hpp>
#include <fstream>
#include <functional>
#include <regex>

#include "log.h"
#include "pcf.h"
#include "util.h"

#define VIADUCT_CONSTIDS "viaduct/fabulous/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
namespace po = boost::program_options;

// Command parser structure for PCF commands using boost::program_options
struct PCFCommand
{
    std::string name;
    po::options_description desc;
    po::positional_options_description pos;
    std::function<void(const po::variables_map &, int)> handler;

    PCFCommand() : desc("") {}
    PCFCommand(const std::string &name, const std::string &description) : name(name), desc(description) {}
};

struct FABulousDesignConstraints
{
    Context *ctx;
    std::string filename;
    int lineno = 0;
    std::map<std::string, PCFCommand> commands;

    FABulousDesignConstraints(Context *ctx, const std::string &filename) : ctx(ctx), filename(filename)
    {
        setup_commands();
    }

    bool parse_loc_from_string(const std::string &s, Loc &loc) const
    {
        static const std::regex loc_re(R"(X(\d+)Y(\d+)/\w+)");
        std::smatch match;
        if (!std::regex_search(s, match, loc_re))
            return false;
        loc = Loc(std::stoi(match[1].str()), std::stoi(match[2].str()), 0);
        return true;
    }

    // Find the iopadmap-created IO cell whose PAD port is connected to `net`.
    // Errors if multiple distinct IO cells share the same net.
    CellInfo *find_pad_peer(NetInfo *net, int line_number) const
    {
        if (!net)
            return nullptr;

        if (net->driver.cell && net->driver.port == id_PAD)
            return net->driver.cell;

        CellInfo *found = nullptr;
        for (auto &usr : net->users) {
            if (usr.port != id_PAD)
                continue;
            if (found && usr.cell != found)
                log_error("Multiple IO cells connected via PAD on net '%s' (on line %d)\n", net->name.c_str(ctx),
                          line_number);
            found = usr.cell;
        }
        return found;
    }

    void execute_set_io_command(const po::variables_map &vm, int line_number)
    {
        std::string cell = vm["cell"].as<std::string>();
        std::string pin = vm["pin"].as<std::string>();

        auto buf_it = ctx->cells.find(ctx->id(cell));
        if (buf_it == ctx->cells.end()) {
            if (ctx->debug)
                log_info("Ignoring constraint for '%s': port does not exist (on line %d)\n", cell.c_str(), line_number);
            return;
        }

        CellInfo *buf_ci = buf_it->second.get();
        if (!buf_ci->type.in(ctx->id("$nextpnr_ibuf"), ctx->id("$nextpnr_obuf"), ctx->id("$nextpnr_iobuf")))
            log_error("Can only constrain IO cells (on line %d)\n", line_number);

        CellInfo *io_cell = find_pad_peer(buf_ci->getPort(id_O), line_number);
        if (!io_cell)
            io_cell = find_pad_peer(buf_ci->getPort(id_I), line_number);
        if (!io_cell)
            log_error("No IO cell found connected to '%s' via PAD port (on line %d). "
                      "Was iopadmap run in Yosys?\n",
                      cell.c_str(), line_number);

        BelId pin_bel = ctx->getBelByNameStr(pin);
        if (pin_bel == BelId())
            log_error("Cannot find a pin named '%s' (on line %d)\n", pin.c_str(), line_number);

        if (ctx->getBelType(pin_bel) != io_cell->type)
            log_error("Pin '%s' bel type '%s' does not match IO cell type '%s' (on line %d)\n", pin.c_str(),
                      ctx->getBelType(pin_bel).c_str(ctx), io_cell->type.c_str(ctx), line_number);

        if (io_cell->attrs.count(id_BEL))
            log_error("duplicate pin constraint on '%s' (on line %d)\n", cell.c_str(), line_number);

        io_cell->attrs[id_BEL] = ctx->getBelName(pin_bel).str(ctx);
        log_info("constrained '%s' to bel '%s'\n", cell.c_str(), io_cell->attrs[id_BEL].as_string().c_str());
    }

    void execute_set_frequency_command(const po::variables_map &vm, int line_number)
    {
        std::string net = vm["net"].as<std::string>();
        float frequency = vm["frequency"].as<float>();

        if (frequency <= 0.0f) {
            log_error("frequency must be positive (on line %d)\n", line_number);
        }

        ctx->addClock(ctx->id(net), frequency);
        log_info("set frequency constraint: %s = %.3f MHz\n", net.c_str(), frequency);
    }

    void execute_set_cell_command(const po::variables_map &vm, int line_number)
    {
        std::string cell = vm["cell"].as<std::string>();
        std::string bel = vm["bel"].as<std::string>();

        auto fnd_cell = ctx->cells.find(ctx->id(cell));
        if (fnd_cell == ctx->cells.end()) {
            log_warning("unmatched constraint '%s' (on line %d)\n", cell.c_str(), line_number);
        } else {
            BelId targetBel = ctx->getBelByNameStr(bel);
            if (targetBel == BelId())
                log_error("package does not have a bel named '%s' (on line %d)\n", bel.c_str(), line_number);
            if (fnd_cell->second->attrs.count(id_BEL))
                log_error("duplicate bel constraint on '%s' (on line %d)\n", cell.c_str(), line_number);
            fnd_cell->second->attrs[id_BEL] = ctx->getBelName(targetBel).str(ctx);
            log_info("constrained '%s' to bel '%s'\n", cell.c_str(),
                     fnd_cell->second->attrs[id_BEL].as_string().c_str());
        }
    }

    void execute_pseudo_plug_command(const po::variables_map &vm, int line_number)
    {
        std::string plug_name = vm["plug-name"].as<std::string>();
        IdString plug_name_id = ctx->id(plug_name);
        size_t port_count = vm.count("port") ? vm["port"].as<std::vector<std::string>>().size() : 0;

        // Find the plug cell
        CellInfo *plug = nullptr;
        if (ctx->cells.count(plug_name_id)) {
            plug = ctx->cells.at(plug_name_id).get();
            if (plug->ports.size() != port_count && port_count > 0) {
                log_error("Port count on pseudo-plug '%s' (%d) does not match number of --port mappings (%ld) (on "
                          "line %d). A pseudo-plug need to be fully constrained\n",
                          plug_name.c_str(), int(plug->ports.size()), port_count, line_number);
            }
        } else {
            log_error("Cannot find cell '%s' (on line %d)\n", plug_name.c_str(), line_number);
        }

        if (!plug->isPseudo())
            ctx->createRegionPlug(plug_name_id, plug->type, Loc(0, 0, 0));

        // Process port mappings
        if (vm.count("port")) {
            const auto &port_specs = vm["port"].as<std::vector<std::string>>();
            for (const auto &mapping : port_specs) {
                // Parse port:wire format
                size_t colon_pos = mapping.find(':');
                if (colon_pos == std::string::npos || colon_pos == 0 || colon_pos == mapping.length() - 1) {
                    log_error("Invalid port:wire mapping format '%s' (expected port:wire) (on line %d)\n",
                              mapping.c_str(), line_number);
                }
                std::string port_name = mapping.substr(0, colon_pos);
                std::string wire_name = mapping.substr(colon_pos + 1);

                Loc wire_loc;
                if (!parse_loc_from_string(wire_name, wire_loc))
                    log_error("Cannot parse location from '%s' (expected X<num>Y<num>/...) (on line %d)\n",
                              wire_name.c_str(), line_number);

                RegionPlug *rplug = dynamic_cast<RegionPlug *>(plug->pseudo_cell.get());
                rplug->loc = wire_loc;

                IdString port_name_id = ctx->id(port_name);
                WireId wire = ctx->getWireByNameStr(wire_name);

                if (wire == WireId())
                    log_error("Cannot find wire '%s' (on line %d)\n", wire_name.c_str(), line_number);

                auto bel_pins = ctx->getBelPinsForCellPin(plug, port_name_id);
                if (bel_pins.empty())
                    log_error("Cannot find port '%s' on cell '%s' (on line %d)\n", port_name.c_str(), plug_name.c_str(),
                              line_number);

                PortType dir;
                if (plug->ports.count(port_name_id)) {
                    dir = plug->ports[port_name_id].type;
                } else {
                    log_error("port '%s' not found on cell '%s' (on line %d)\n", port_name.c_str(), plug_name.c_str(),
                              line_number);
                }

                if (bel_pins.size() > 1)
                    log_warning("Port '%s' on cell '%s' has multiple possible pin mappings, using first\n",
                                port_name.c_str(), plug_name.c_str());

                ctx->addPlugPin(plug_name_id, bel_pins[0], dir, wire);
                log_info("constrained pseudo-plug '%s' port '%s' to wire '%s'\n", plug_name.c_str(), port_name.c_str(),
                         wire_name.c_str());
            }
        }

        // Process timing constraints
        if (vm.count("timing")) {
            log_warning("Timing constraints on pseudo-plugs are currently NOT respected and will be implemented in the "
                        "future (on line %d)\n",
                        line_number);
            const auto &timing_specs = vm["timing"].as<std::vector<std::string>>();
            for (const auto &spec : timing_specs) {
                // Parse port-in:port-out:min-delay:max-delay format
                std::vector<std::string> parts;
                std::stringstream spec_ss(spec);
                std::string part;
                while (std::getline(spec_ss, part, ':')) {
                    parts.push_back(part);
                }

                if (parts.size() != 4) {
                    log_error("Invalid timing constraint format '%s' (expected "
                              "port-in:port-out:min-delay:max-delay) (on line %d)\n",
                              spec.c_str(), line_number);
                }

                std::string port_in = parts[0];
                std::string port_out = parts[1];
                float min_delay = 0.0f, max_delay = 0.0f;
                try {
                    min_delay = std::stof(parts[2]);
                    max_delay = std::stof(parts[3]);
                } catch (const std::exception &) {
                    log_error("Invalid numeric value in timing constraint '%s' (on line %d)\n", spec.c_str(),
                              line_number);
                }

                if (min_delay < 0.0f) {
                    log_error("min-delay must be non-negative in timing constraint '%s' (on line %d)\n", spec.c_str(),
                              line_number);
                }
                if (max_delay < 0.0f) {
                    log_error("max-delay must be non-negative in timing constraint '%s' (on line %d)\n", spec.c_str(),
                              line_number);
                }
                if (min_delay > max_delay) {
                    log_error("min-delay (%.3f) cannot be greater than max-delay (%.3f) in timing constraint '%s' "
                              "(on line %d)\n",
                              min_delay, max_delay, spec.c_str(), line_number);
                }

                IdString port_in_id = ctx->id(port_in);
                IdString port_out_id = ctx->id(port_out);

                if (!plug->ports.count(port_in_id) || plug->ports.at(port_in_id).type != PORT_IN)
                    log_error("input port '%s' not found on cell '%s' (on line %d)\n", port_in.c_str(),
                              plug_name.c_str(), line_number);
                if (!plug->ports.count(port_out_id) || plug->ports.at(port_out_id).type != PORT_OUT)
                    log_error("output port '%s' not found on cell '%s' (on line %d)\n", port_out.c_str(),
                              plug_name.c_str(), line_number);

                ctx->addCellTimingDelayMinMax(plug_name_id, port_in_id, port_out_id, min_delay, max_delay);
                log_info("applied timing constraint %.3f-%.3f ns from port '%s' to port '%s' on pseudo-plug '%s'\n",
                         min_delay, max_delay, port_in.c_str(), port_out.c_str(), plug_name.c_str());
            }
        }
    }

    void execute_prohibit_pip_command(const po::variables_map &vm, int line_number)
    {
        std::string pip_pattern = vm["pip"].as<std::string>();
        std::string dummy_net_name = "$prohibit_pip";
        NetInfo *dummy_net = nullptr;
        if (ctx->nets.count(ctx->id(dummy_net_name)))
            dummy_net = ctx->nets.at(ctx->id(dummy_net_name)).get();
        else
            dummy_net = ctx->createNet(ctx->id(dummy_net_name));

        try {
            std::regex pip_regex(pip_pattern);
            std::vector<PipId> matching_pips;

            // Find all pips matching the regex pattern
            for (auto pip : ctx->getPips()) {
                std::string pip_name = ctx->nameOfPip(pip);
                if (std::regex_match(pip_name, pip_regex)) {
                    matching_pips.push_back(pip);
                }
            }

            if (matching_pips.empty()) {
                log_error("No pips found matching pattern '%s' (on line %d)\n", pip_pattern.c_str(), line_number);
            }

            // Prohibit all matching pips
            for (auto pip : matching_pips) {
                ctx->bindPip(pip, dummy_net, STRENGTH_USER);
                if (ctx->debug)
                    log_info("forbade pip '%s' by binding to dummy net '%s'\n", ctx->nameOfPip(pip),
                             dummy_net_name.c_str());
            }

            log_info("Prohibited %d pips matching pattern '%s'\n", static_cast<int>(matching_pips.size()),
                     pip_pattern.c_str());

        } catch (const std::regex_error &e) {
            log_error("Invalid regex pattern '%s' in prohibit_pip command (on line %d): %s\n", pip_pattern.c_str(),
                      line_number, e.what());
        }
    }

    void execute_prohibit_wire_command(const po::variables_map &vm, int line_number)
    {
        std::string wire_pattern = vm["wire"].as<std::string>();
        std::string dummy_net_name = "$prohibit_wire";
        NetInfo *dummy_net = nullptr;
        if (ctx->nets.count(ctx->id(dummy_net_name)))
            dummy_net = ctx->nets.at(ctx->id(dummy_net_name)).get();
        else
            dummy_net = ctx->createNet(ctx->id(dummy_net_name));

        try {
            std::regex wire_regex(wire_pattern);
            std::vector<WireId> matching_wires;

            // Find all wires matching the regex pattern
            for (auto wire : ctx->getWires()) {
                std::string wire_name = ctx->nameOfWire(wire);
                if (std::regex_match(wire_name, wire_regex)) {
                    matching_wires.push_back(wire);
                }
            }

            if (matching_wires.empty()) {
                log_error("No wires found matching pattern '%s' (on line %d)\n", wire_pattern.c_str(), line_number);
            }

            // Prohibit all matching wires
            for (auto wire : matching_wires) {
                ctx->bindWire(wire, dummy_net, STRENGTH_USER);
                if (ctx->debug)
                    log_info("forbade wire '%s' by binding to dummy net '%s'\n", ctx->nameOfWire(wire),
                             dummy_net_name.c_str());
            }

            log_info("Prohibited %d wires matching pattern '%s'\n", static_cast<int>(matching_wires.size()),
                     wire_pattern.c_str());

        } catch (const std::regex_error &e) {
            log_error("Invalid regex pattern '%s' in prohibit_wire command (on line %d): %s\n", wire_pattern.c_str(),
                      line_number, e.what());
        }
    }

    void execute_prohibit_bel_command(const po::variables_map &vm, int line_number)
    {
        std::string bel_pattern = vm["bel"].as<std::string>();

        try {
            std::regex bel_regex(bel_pattern);
            std::vector<BelId> matching_bels;

            // Find all bels matching the regex pattern
            for (auto bel : ctx->getBels()) {
                std::string bel_name = ctx->nameOfBel(bel);
                if (std::regex_match(bel_name, bel_regex)) {
                    matching_bels.push_back(bel);
                }
            }

            if (matching_bels.empty()) {
                log_error("No bels found matching pattern '%s' (on line %d)\n", bel_pattern.c_str(), line_number);
            }

            // Prohibit all matching bels
            for (auto bel : matching_bels) {
                std::string bel_name = ctx->nameOfBel(bel);
                std::string dummy_cell_name = "$prohibit_bel_" + bel_name;
                IdString bel_type = ctx->getBelType(bel);
                CellInfo *dummy_cell = ctx->createCell(ctx->id(dummy_cell_name), bel_type);

                ctx->bindBel(bel, dummy_cell, STRENGTH_USER);
                if (ctx->debug)
                    log_info("forbade bel '%s' by binding to dummy cell '%s'\n", bel_name.c_str(),
                             dummy_cell_name.c_str());
            }

            log_info("Prohibited %d bels matching pattern '%s'\n", static_cast<int>(matching_bels.size()),
                     bel_pattern.c_str());

        } catch (const std::regex_error &e) {
            log_error("Invalid regex pattern '%s' in prohibit_bel command (on line %d): %s\n", bel_pattern.c_str(),
                      line_number, e.what());
        }
    }

    void setup_commands()
    {
        // Setup set_io command
        // Syntax: set_io cell pin
        commands.emplace("set_io", PCFCommand("set_io", "Constrain IO cell to pin"));
        auto &set_io = commands.at("set_io");
        set_io.desc.add_options()("cell", po::value<std::string>()->required(),
                                  "Cell name")("pin", po::value<std::string>()->required(), "Pin name");
        set_io.pos.add("cell", 1);
        set_io.pos.add("pin", 1);
        set_io.handler = [this](const po::variables_map &vm, int line_number) {
            execute_set_io_command(vm, line_number);
        };

        // Setup set_frequency command
        // Syntax: set_frequency net frequency
        commands.emplace("set_frequency", PCFCommand("set_frequency", "Set clock frequency constraint"));
        auto &set_freq = commands.at("set_frequency");
        set_freq.desc.add_options()("net", po::value<std::string>()->required(),
                                    "Net name")("frequency", po::value<float>()->required(), "Frequency in MHz");
        set_freq.pos.add("net", 1);
        set_freq.pos.add("frequency", 1);
        set_freq.handler = [this](const po::variables_map &vm, int line_number) {
            execute_set_frequency_command(vm, line_number);
        };

        // Setup set_cell command
        // Syntax: set_cell cell bel
        commands.emplace("set_cell", PCFCommand("set_cell", "Constrain cell to bel"));
        auto &set_cell = commands.at("set_cell");
        set_cell.desc.add_options()("cell", po::value<std::string>()->required(),
                                    "Cell name")("bel", po::value<std::string>()->required(), "Bel name");
        set_cell.pos.add("cell", 1);
        set_cell.pos.add("bel", 1);
        set_cell.handler = [this](const po::variables_map &vm, int line_number) {
            execute_set_cell_command(vm, line_number);
        };

        // Setup set_pseudo_plug command with flag-based interface and timing constraints support
        // Syntax: set_pseudo_plug plug_name --port <port>:<wire> --timing <port-in>:<port-out>:<min>:<max>
        commands.emplace("set_pseudo_plug",
                         PCFCommand("set_pseudo_plug",
                                    "Configure pseudo plug with flag-based port mappings and timing constraints"));
        auto &pseudo_plug = commands.at("set_pseudo_plug");
        pseudo_plug.desc.add_options()("plug-name", po::value<std::string>()->required(), "Pseudo plug cell name")(
                "port", po::value<std::vector<std::string>>()->multitoken(),
                "Port mapping in format port:wire (repeatable)")(
                "timing", po::value<std::vector<std::string>>()->multitoken(),
                "Timing constraint in format port-in:port-out:min-delay:max-delay (repeatable)");
        pseudo_plug.pos.add("plug-name", 1);
        pseudo_plug.handler = [this](const po::variables_map &vm, int line_number) {
            execute_pseudo_plug_command(vm, line_number);
        };

        // Setup prohibit_pip command
        // Syntax: prohibit_pip pip_pattern
        commands.emplace("prohibit_pip", PCFCommand("prohibit_pip", "Prohibit use of pips matching regex pattern"));
        auto &prohibit_pip = commands.at("prohibit_pip");
        prohibit_pip.desc.add_options()("pip", po::value<std::string>()->required(),
                                        "Pip name pattern (regex) to prohibit");
        prohibit_pip.pos.add("pip", 1);
        prohibit_pip.handler = [this](const po::variables_map &vm, int line_number) {
            execute_prohibit_pip_command(vm, line_number);
        };

        // Setup prohibit_wire command
        // Syntax: prohibit_wire wire_pattern
        commands.emplace("prohibit_wire", PCFCommand("prohibit_wire", "Prohibit use of wires matching regex pattern"));
        auto &prohibit_wire = commands.at("prohibit_wire");
        prohibit_wire.desc.add_options()("wire", po::value<std::string>()->required(),
                                         "Wire name pattern (regex) to prohibit");
        prohibit_wire.pos.add("wire", 1);
        prohibit_wire.handler = [this](const po::variables_map &vm, int line_number) {
            execute_prohibit_wire_command(vm, line_number);
        };

        // Setup prohibit_bel command
        // Syntax: prohibit_bel bel_pattern
        commands.emplace("prohibit_bel", PCFCommand("prohibit_bel", "Prohibit use of bels matching regex pattern"));
        auto &prohibit_bel = commands.at("prohibit_bel");
        prohibit_bel.desc.add_options()("bel", po::value<std::string>()->required(),
                                        "Bel name pattern (regex) to prohibit");
        prohibit_bel.pos.add("bel", 1);
        prohibit_bel.handler = [this](const po::variables_map &vm, int line_number) {
            execute_prohibit_bel_command(vm, line_number);
        };
    }

    bool parse_and_execute_command(const std::vector<std::string> &words)
    {
        if (words.empty())
            return false;

        std::string cmd_name = words[0];
        auto it = commands.find(cmd_name);
        if (it == commands.end()) {
            log_error("unsupported command '%s' (on line %d)\n", cmd_name.c_str(), lineno);
            return false;
        }

        try {
            // Convert words to argc/argv style for program_options
            std::vector<const char *> argv;
            for (const auto &word : words) {
                argv.push_back(word.c_str());
            }

            po::variables_map vm;

            po::parsed_options parsed = po::command_line_parser(static_cast<int>(argv.size()), argv.data())
                                                .options(it->second.desc)
                                                .positional(it->second.pos)
                                                .run();

            po::store(parsed, vm);
            po::notify(vm);

            // Execute the command
            it->second.handler(vm, lineno);
            return true;

        } catch (const po::error &e) {
            log_error("Error parsing command '%s' on line %d: %s\n", cmd_name.c_str(), lineno, e.what());
        } catch (const std::exception &e) {
            log_error("Error executing command '%s' on line %d: %s\n", cmd_name.c_str(), lineno, e.what());
        }
        return false;
    }

    void apply_constraints()
    {
        std::ifstream in(filename);
        if (!in) {
            log_error("failed to open constraint file\n");
        }

        std::string line;
        std::string accumulated_line;
        lineno = 0;
        int command_start_line = 0;

        while (std::getline(in, line)) {
            lineno++;

            // Remove comments before checking for continuation
            size_t cstart = line.find('#');
            if (cstart != std::string::npos)
                line = line.substr(0, cstart);

            // Trim trailing whitespace
            while (!line.empty() && std::isspace(line.back()))
                line.pop_back();

            // Check for line continuation (ends with backslash)
            bool has_continuation = false;
            if (!line.empty() && line.back() == '\\') {
                has_continuation = true;
                line.pop_back(); // Remove the backslash
                // Trim any whitespace before the backslash
                while (!line.empty() && std::isspace(line.back()))
                    line.pop_back();
            }

            // If this is the start of a new command, record the line number
            if (accumulated_line.empty() && !line.empty()) {
                command_start_line = lineno;
            }

            // Accumulate the line
            if (!accumulated_line.empty() && !line.empty()) {
                accumulated_line += " "; // Add space between continued lines
            }
            accumulated_line += line;

            // If no continuation, process the accumulated command
            if (!has_continuation) {
                // Parse the accumulated command
                std::stringstream ss(accumulated_line);
                std::vector<std::string> words;
                std::string tmp;
                while (ss >> tmp)
                    words.push_back(tmp);

                if (!words.empty()) {
                    // Set line number to the start of the command for error reporting
                    int saved_lineno = lineno;
                    lineno = command_start_line;
                    parse_and_execute_command(words);
                    lineno = saved_lineno;
                }

                // Reset for next command
                accumulated_line.clear();
                command_start_line = 0;
            }
        }

        // Handle case where file ends with a continuation (error)
        if (!accumulated_line.empty()) {
            log_error("File ends with incomplete command starting at line %d (missing continuation or final command)\n",
                      command_start_line);
        }
    }
};
} // namespace

void fabulous_pcf(Context *ctx, const std::string &filename)
{
    FABulousDesignConstraints PCF(ctx, filename);
    PCF.apply_constraints();
    log_info("Finished applying constraints from '%s'\n", filename.c_str());
}

NEXTPNR_NAMESPACE_END