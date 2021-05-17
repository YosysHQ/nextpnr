package net.gatecat.fpga.xlnxic;

import com.xilinx.rapidwright.design.Cell;
import com.xilinx.rapidwright.design.CellPinStaticDefaults;
import com.xilinx.rapidwright.design.Design;
import com.xilinx.rapidwright.design.DesignTools;
import com.xilinx.rapidwright.design.NetType;
import com.xilinx.rapidwright.design.SiteInst;
import com.xilinx.rapidwright.design.Unisim;
import com.xilinx.rapidwright.design.VivadoProp;
import com.xilinx.rapidwright.design.VivadoPropType;

import com.xilinx.rapidwright.device.PartNameTools;
import com.xilinx.rapidwright.device.IOBankType;
import com.xilinx.rapidwright.device.Package;
import com.xilinx.rapidwright.device.*;
import com.xilinx.rapidwright.edif.*;
import com.xilinx.rapidwright.util.Utils;
import com.xilinx.rapidwright.util.RapidWright;
import com.xilinx.rapidwright.util.CountingOutputStream;
import com.xilinx.rapidwright.timing.*;
import com.xilinx.rapidwright.interchange.EnumerateCellBelMapping;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.io.IOException;
import java.util.*;

import java.security.NoSuchAlgorithmException;
import java.security.MessageDigest;

public class bbaexport {
    static long est_database_size = 0;

    private static ArrayList<String> constIds = new ArrayList<>();
    private static HashMap<String, Integer> knownConstIds = new HashMap<>();

    private static int makeConstId(String s) {
        if (knownConstIds.containsKey(s))
            return knownConstIds.get(s);
        knownConstIds.put(s, constIds.size());
        constIds.add(s);
        return constIds.size() - 1;
    }

    public static HashMap<String, ArrayList<String>> bel_to_celltypes = new HashMap();

    private static void init_bel_to_celltypes(Design des) {
        Device dev = des.getDevice();
        for (EDIFCell prim : Design.getPrimitivesLibrary(dev.getName()).getCells()) {
            Cell c = des.createCell("inst", prim);
            try {
                for (Map.Entry<SiteTypeEnum, Set<String>> entry : c.getCompatiblePlacements().entrySet()) {
                    for (String bel : entry.getValue()) {
                        String key = entry.getKey().toString() + ":" + bel;
                        if (!bel_to_celltypes.containsKey(key))
                            bel_to_celltypes.put(key, new ArrayList());
                        bel_to_celltypes.get(key).add(c.getType());
                    }
                }
            } catch (java.lang.RuntimeException e) {

            }
            des.removeCell(c);
            des.getNetlist().getTopCell().removeCellInst("inst");
        }
    }

    private static class NextpnrParam {
        public int key;
        public int value;
        NextpnrParam(String name, EDIFPropertyValue value) {
            this.key = makeConstId(name);
            this.value = makeConstId(value.toString());
        }
        NextpnrParam(String name, String value) {
            this.key = makeConstId(name);
            this.value = makeConstId(value);
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", key);
            bba.printf("u32 %d\n", value);
        }
    }

    private static class NextpnrPinMapEntry {
        public NextpnrPinMapEntry(int log_pin, int[] phys_pins) {
            this.log_pin = log_pin;
            this.phys_pins = phys_pins;
        }
        public NextpnrPinMapEntry(String log_pin, LinkedHashSet<String> phys_pins) {
            this.log_pin = makeConstId(log_pin);
            this.phys_pins = new int[phys_pins.size()];
            int idx = 0;
            for (String phys : phys_pins)
                this.phys_pins[idx++] = makeConstId(phys);
        }
        public int log_pin;
        public int[] phys_pins;
    }

    private static class NextpnrParameterPinMap {
        public NextpnrParameterPinMap(NextpnrParam[] param_matches, NextpnrPinMapEntry[] pins) {
            this.param_matches = param_matches;
            this.pins = pins;
        }
        public NextpnrParam[] param_matches;
        public NextpnrPinMapEntry[] pins;
    }

    private static class NextpnrPinMap {
        public NextpnrPinMap(NextpnrPinMapEntry[] common_pins, NextpnrParameterPinMap[] param_pins) {
            this.common_pins = common_pins;
            this.param_pins = param_pins;
        }
        public NextpnrPinMapEntry[] common_pins;
        public NextpnrParameterPinMap[] param_pins;

        public String hash() throws NoSuchAlgorithmException {
            MessageDigest md = MessageDigest.getInstance("md5");
            byte[] tempbuf = {0, 0, 0, 0};
            java.util.function.Consumer<Integer> add_int = (Integer x) -> {
                tempbuf[0] = (byte)(x >> 24);
                tempbuf[1] = (byte)(x >> 16);
                tempbuf[2] = (byte)(x >> 8);
                tempbuf[3] = (byte)(x >> 0);
                md.update(tempbuf);
            };
            add_int.accept(common_pins.length);
            for (NextpnrPinMapEntry p : common_pins) {
                add_int.accept(p.log_pin);
                add_int.accept(p.phys_pins.length);
                for (int phys : p.phys_pins)
                    add_int.accept(phys);
            }
            if (param_pins != null) {
                add_int.accept(param_pins.length);
                for (NextpnrParameterPinMap pm : param_pins) {
                    add_int.accept(pm.param_matches.length);
                    for (NextpnrParam m : pm.param_matches) {
                        add_int.accept(m.key);
                        add_int.accept(m.value);
                    }
                    add_int.accept(pm.pins.length);
                    for (NextpnrPinMapEntry p : pm.pins) {
                        add_int.accept(p.log_pin);
                        add_int.accept(p.phys_pins.length);
                        for (int phys : p.phys_pins)
                            add_int.accept(phys);
                    }
                }
            } else {
                add_int.accept(0);
            }
            byte[] digest = md.digest();
            return Base64.getEncoder().encodeToString(digest);
        }

        public void write_content_bba(int index, PrintWriter bba) {
            for (NextpnrPinMapEntry e : common_pins) {
                bba.printf("label pins%d_common_%d_phys\n", index, e.log_pin);
                for (int phys_pin : e.phys_pins)
                    bba.printf("u32 %d\n", phys_pin);
            }
            bba.printf("label pins%d_common\n", index);
            for (NextpnrPinMapEntry e : common_pins) {
                bba.printf("u32 %d\n", e.log_pin);
                bba.printf("ref pins%d_common_%d_phys\n", index, e.log_pin);
                bba.printf("u32 %d\n", e.phys_pins.length);
            }
            if (param_pins != null)
                for (int j = 0; j < param_pins.length; j++) {
                    NextpnrParameterPinMap pp = param_pins[j];
                    for (NextpnrPinMapEntry e : pp.pins) {
                        bba.printf("label pins%d_p%d_%d_phys\n", index, j, e.log_pin);
                        for (int phys_pin : e.phys_pins)
                            bba.printf("u32 %d\n", phys_pin);
                    }
                    bba.printf("label pins%d_p%d_params\n", index, j);
                    for (NextpnrParam param : pp.param_matches)
                        param.write_bba(bba);
                    bba.printf("label pins%d_p%d_pins\n", index, j);
                    for (NextpnrPinMapEntry e : pp.pins) {
                        bba.printf("u32 %d\n", e.log_pin);
                        bba.printf("ref pins%d_p%d_%d_phys\n", index, j, e.log_pin);
                        bba.printf("u32 %d\n", e.phys_pins.length);
                    }
                }
            bba.printf("label pins%d_param\n", index);
            if (param_pins != null)
                for (int j = 0; j < param_pins.length; j++) {
                    NextpnrParameterPinMap pp = param_pins[j];
                    bba.printf("ref pins%d_p%d_params\n", index, j);
                    bba.printf("u32 %d\n", pp.param_matches.length);
                    bba.printf("ref pins%d_p%d_pins\n", index, j);
                    bba.printf("u32 %d\n", pp.pins.length);
                }
        }
        public void write_bba(int index, PrintWriter bba) {
            bba.printf("ref pins%d_common\n", index);
            bba.printf("u32 %d\n", common_pins.length);
            bba.printf("ref pins%d_param\n", index);
            bba.printf("u32 %d\n", param_pins == null ? 0 : param_pins.length);
        }
    }

    private static NextpnrPinMap make_pin_mapping(Design des, SiteInst si, BEL bel, String cell_type) {
        LinkedHashSet<String> all_log_pins = new LinkedHashSet();
        ArrayList<LinkedHashMap<String, LinkedHashSet<String>>> pin_maps = new ArrayList();
        List<List<String>> parameterSets = EnumerateCellBelMapping.getParametersFor(des.getDevice().getSeries(), cell_type);
        for (List<String> params : parameterSets) {
            String[] param_array = params.toArray(new String[params.size()]);
            Cell c = des.createAndPlaceCell(des.getNetlist().getTopCell(), "inst",
                Unisim.valueOf(cell_type), si.getSite(), bel, param_array);

            LinkedHashMap<String, LinkedHashSet<String>> pin_map = new LinkedHashMap();
            for (Map.Entry<String, Set<String>> l2p : c.getPinMappingsL2P().entrySet()) {
                LinkedHashSet<String> phys = new LinkedHashSet();
                all_log_pins.add(l2p.getKey());
                phys.addAll(l2p.getValue());
                pin_map.put(l2p.getKey(), phys);
            }

            pin_maps.add(pin_map);

            des.removeCell(c);
            des.getNetlist().getTopCell().removeCellInst("inst");
        }

        ArrayList<NextpnrPinMapEntry> common_pins = new ArrayList();
        ArrayList<ArrayList<NextpnrPinMapEntry>> param_pins = new ArrayList();
        for (int i = 0; i < parameterSets.size(); i++)
            param_pins.add(new ArrayList());
        boolean has_param_pins = false;
        for (String log_pin : all_log_pins) {
            // Determine if the pin is the same across all param sets or param-dependent
            boolean is_common = false;
            if (pin_maps.get(0).containsKey(log_pin)) {
                is_common = true;
                LinkedHashSet<String> p0_phys = pin_maps.get(0).get(log_pin);
                for (int i = 1; i < pin_maps.size(); i++) {
                    if (!pin_maps.get(i).containsKey(log_pin) || !pin_maps.get(i).get(log_pin).equals(p0_phys)) {
                        is_common = false;
                        break;
                    }
                }
            }
            if (is_common) {
                // Convert and add to the set of common pins
                common_pins.add(new NextpnrPinMapEntry(log_pin, pin_maps.get(0).get(log_pin)));
            } else {
                has_param_pins = true;
                for (int i = 0; i < pin_maps.size(); i++) {
                    // Add a parameter pin entry, if the mapping is relevant
                    if (pin_maps.get(i).containsKey(log_pin))
                        param_pins.get(i).add(new NextpnrPinMapEntry(log_pin, pin_maps.get(i).get(log_pin)));
                }
            }
        }
        NextpnrPinMapEntry[] common_pins_arr = common_pins.toArray(new NextpnrPinMapEntry[common_pins.size()]);
        NextpnrParameterPinMap[] param_pins_arr = null;
        if (has_param_pins) {
            param_pins_arr = new NextpnrParameterPinMap[param_pins.size()];
            for (int i = 0; i < parameterSets.size(); i++) {
                NextpnrParam[] param_arr = new NextpnrParam[parameterSets.get(i).size()];
                int param_idx = 0;
                for (String param : parameterSets.get(i)) {
                    String[] param_split = param.split("=", 2);
                    param_arr[param_idx++] = new NextpnrParam(param_split[0], param_split[1]);
                }
                NextpnrPinMapEntry[] pins_arr = param_pins.get(i).toArray(new NextpnrPinMapEntry[param_pins.get(i).size()]);
                param_pins_arr[i] = new NextpnrParameterPinMap(param_arr, pins_arr);
            }
        }
        return new NextpnrPinMap(common_pins_arr, param_pins_arr);
    }

    // The set of all the different node shapes
    private static ArrayList<NextpnrPinMap> pin_maps = new ArrayList<>();
    private static HashMap<String, Integer> pin_maps_by_hash = new HashMap();

    static class NextpnrBelPinRef {
        public NextpnrBelPinRef(int bel, String port) {
            this.bel = bel;
            this.port = makeConstId(port);
        }
        public int bel;
        public int port;
    }

    static class NextpnrWire {
        public NextpnrWire(String name, int index, int intent) {
            this.name = makeConstId(name);
            this.index = index;
            this.site = -1;
            this.site_variant = -1;
            this.intent = intent;
            this.flags = 0;
            this.pips_uh = new ArrayList<>();
            this.pips_dh = new ArrayList<>();
            this.belpins = new ArrayList<>();
        }

        public NextpnrWire(String name, int index, int intent, int site, int site_variant) {
            this(name, index, intent);
            this.site = site;
            this.site_variant = site_variant;
        }

        public int name;
        public int index;
        public int site;
        public int site_variant;
        public int intent;
        public int flags;
        public ArrayList<Integer> pips_uh, pips_dh;
        public ArrayList<NextpnrBelPinRef> belpins;
    }

    static private int PIP_FLAG_CAN_INV = 0x400;
    static private int PIP_FLAG_FIXED_INV = 0x800;
    static private int PIP_FLAG_PSEUDO = 0x1000;
    static private int PIP_FLAG_SYNTHETIC = 0x2000;
    static private int PIP_FLAG_REVERSED = 0x4000;

    static class NextpnrPseudoPipPin {
        public NextpnrPseudoPipPin(int bel_index, String pin_name) {
            this.bel_index = bel_index;
            this.pin_name = makeConstId(pin_name);
        }
        public int bel_index;
        public int pin_name;
    }

    static class NextpnrPip {
        public NextpnrPip(int index, int src_wire, int dst_wire, NextpnrPipType type) {
            this.index = index;
            this.src_wire = src_wire;
            this.dst_wire = dst_wire;
            this.type = type;
        }

        public NextpnrPip(int index, int src_wire, int dst_wire, NextpnrPipType type, int site, int site_variant) {
            this(index, src_wire, dst_wire, type);
            this.site = site;
            this.site_variant = site_variant;
        }

        public NextpnrPip(int index, int src_wire, int dst_wire, NextpnrPipType type, int site, int site_variant, String site_port) {
            this(index, src_wire, dst_wire, type, site, site_variant);
            this.site_port = makeConstId(site_port);
        }

        public NextpnrPip(int index, int src_wire, int dst_wire, NextpnrPipType type, int site, int site_variant, int bel_idx, int from_pin, int to_pin) {
            this(index, src_wire, dst_wire, type, site, site_variant);
            this.bel = bel_idx;
            this.from_pin = from_pin;
            this.to_pin = to_pin;
        }

        public int index;
        public int src_wire, dst_wire;
        public NextpnrPipType type;

        public int flags = 0;
        public int bel = -1;
        public int from_pin = 0;
        public int to_pin = 0;
        public int site = -1;
        public int site_variant = -1;

        public int site_port = -1;
        public ArrayList<NextpnrPseudoPipPin> pp_pins = null;
    }


    enum NextpnrPipType {
        TILE_ROUTING,
        SITE_ENTRANCE,
        SITE_EXIT,
        SITE_INTERNAL,
        LUT_PERMUTATION,
        LUT_ROUTETHRU,
        CONST_DRIVER,
    }

    enum NextpnrPortDir {
        IN,
        OUT,
        INOUT,
    }

    static class NextpnrBelWire {
        public int name;
        public int wire;
        public NextpnrPortDir port_type;
    }

    static class NextpnrBelCellMap {
        public NextpnrBelCellMap(String cell_type, int pin_map_idx) {
            this.cell_type = makeConstId(cell_type);
            this.pin_map_idx = pin_map_idx;
        }
        public int cell_type;
        public int pin_map_idx;
    }

    static ArrayList<NextpnrBelCellMap> get_bel_placements(Design des, SiteInst si, BEL bel) throws NoSuchAlgorithmException {
        ArrayList<NextpnrBelCellMap> result = new ArrayList();
        String key = si.getSiteTypeEnum().toString() + ":" + bel.getName();
        if (!bel_to_celltypes.containsKey(key))
            return result;
        for (String cell_type : bel_to_celltypes.get(key)) {
            NextpnrPinMap pin_map = make_pin_mapping(des, si, bel, cell_type);
            String pin_map_hash = pin_map.hash();
            if (!pin_maps_by_hash.containsKey(pin_map_hash)) {
                int idx = pin_maps.size();
                pin_maps.add(pin_map);
                pin_maps_by_hash.put(pin_map_hash, idx);
            }
            result.add(new NextpnrBelCellMap(cell_type, pin_maps_by_hash.get(pin_map_hash)));
        }
        return result;
    }

    static private int BEL_FLAG_RBEL = 0x1000;
    static private int BEL_FLAG_PAD = 0x2000;

    static class NextpnrTileTypeSite {
        public NextpnrTileTypeSite(Tile t, Site s) {
            this.index = s.getSiteIndexInTile();
            this.prefix = makeConstId(s.getNameSpacePrefix());
            // Determine the lowest site in tile with same prefix to get relative coordinates
            int min_x = 999999, min_y = 999999;
            for (Site s2 : t.getSites()) {
                if (s2.getNameSpacePrefix() != s.getNameSpacePrefix())
                    continue;
                min_x = Math.min(min_x, s2.getInstanceX());
                min_y = Math.min(min_y, s2.getInstanceY());
            }
            this.dx = s.getInstanceX() - min_x;
            this.dy = s.getInstanceY() - min_y;
            variant_types = new ArrayList();
            variant_types.add(makeConstId(s.getSiteTypeEnum().toString()));
            for (SiteTypeEnum alt : s.getAlternateSiteTypeEnums())
                variant_types.add(makeConstId(alt.toString()));
        }
        public int index;
        public int prefix;
        public ArrayList<Integer> variant_types;
        public int dx;
        public int dy;
    }

    static class NextpnrBel {
        public NextpnrBel(String name, int index, String type, int site, int site_variant, int z) {
            this.name = makeConstId(name);
            this.index = index;
            this.bel_type = makeConstId(type);
            this.site = site;
            this.site_variant = site_variant;
            this.flags = 0;
            this.z = z;
            this.place_idx = -1;
            this.bel_ports = new ArrayList();
            this.placements = new ArrayList();
        }

        public int name;
        public int index;
        public int bel_type;
        public int site;
        public int site_variant;
        public int flags;
        public int z;
        public int place_idx;
        public ArrayList<NextpnrBelWire> bel_ports;
        public ArrayList<NextpnrBelCellMap> placements;
    }

    public static HashMap<TileTypeEnum, DedupTileFlatWires> flat_tiles = new HashMap();
    public static HashMap<TileTypeEnum, Integer> tile_type_idx = new HashMap();
    public static HashSet<String> pad_bels = new HashSet();

    static class NextpnrTileType {
        public int index;
        public int type;
        public ArrayList<NextpnrBel> bels;
        public ArrayList<NextpnrWire> wires;
        public ArrayList<NextpnrPip> pips;
        public ArrayList<NextpnrTileTypeSite> sites;

        public int tile_wire_count = 0; // excluding site wires
        private DedupTileFlatWires wire_lookup;

        public HashMap<String, Integer> site_wire_to_wire_idx;
        public HashMap<String, Integer> bel_to_idx;

        public int row_gnd_wire_index, row_vcc_wire_index, global_gnd_wire_index, global_vcc_wire_index;

        // Convert wire name in site to tile wire index, adding a new tile wire if needed
        private int site_wire_to_wire(SiteInst s, int site_variant, String wire) {
            int site_idx = s.getSite().getSiteIndexInTile();
            String key = s.getSiteTypeEnum().toString() + site_idx + "/" + wire;
            if (site_wire_to_wire_idx.containsKey(key))
                return site_wire_to_wire_idx.get(key);
            NextpnrWire nw = new NextpnrWire(wire, wires.size(), 0, site_idx, site_variant);
            if (wire.equals("GND_WIRE"))
                nw.intent = makeConstId("INTENT_SITE_GND");
            else
                nw.intent = makeConstId("INTENT_SITE_WIRE");
            wires.add(nw);
            site_wire_to_wire_idx.put(key, nw.index);
            return nw.index;
        }

        private int lookup_logic_postfix(String p) {
            // These are for all the logic bels that are duplicated across each 'eigth' of a SLICE,
            // with the first char of the name being a letter A..H
            switch (p) {
                case "6LUT": return 0;
                case "5LUT": return 1;
                case "FF": return 2;
                case "5FF": return 3; // xc7 style
                case "FF2": return 3; // Versal/US+ style
                // Versal input registers
                case "1_IMR": return 5;
                case "2_IMR": return 6;
                case "3_IMR": return 7;
                case "4_IMR": return 8;
                case "5_IMR": return 9;
                case "6_IMR": return 10;
                case "I_IMR": return 11;
                case "X_IMR": return 12;
            };
            return -1;
        }

        private int get_bel_place_idx(SiteInst s, BEL b) {
            // Determine the lowest site in tile with same prefix to get relative coordinates
            int min_x = 999999, min_y = 999999;
            for (Site s2 : s.getTile().getSites()) {
                if (s2.getNameSpacePrefix() != s.getSite().getNameSpacePrefix())
                    continue;
                min_x = Math.min(min_x, s2.getInstanceX());
                min_y = Math.min(min_y, s2.getInstanceY());
            }
            int dx = s.getSite().getInstanceX() - min_x;
            int dy = s.getSite().getInstanceY() - min_y;

            TileTypeEnum tt = s.getTile().getTileTypeEnum();
            SiteTypeEnum st = s.getSite().getSiteTypeEnum();
            if (st == SiteTypeEnum.SLICEL || st == SiteTypeEnum.SLICEM) {
                String subslices = "ABCDEFGH";
                // Postfixes are for the bels that are duplicated for each eigth
                String n = b.getName();
                int postfix_idx = lookup_logic_postfix(n.substring(1));
                if (postfix_idx != -1) {
                    return (subslices.indexOf(n.charAt(0)) << 4) | postfix_idx;
                }
                // Special bels
                switch (n) {
                    // 7-series
                    case "CARRY4": return 0x04;
                    case "F7AMUX": return 0x05;
                    case "F7BMUX": return 0x25;
                    case "F8MUX":  return  0x06;
                    // UltraScale+
                    case "CARRY8":    return 0x04;
                    case "F7MUX_AB":  return 0x05;
                    case "F7MUX_CD":  return 0x25;
                    case "F7MUX_EF":  return 0x45;
                    case "F7MUX_GH":  return 0x65;
                    case "F8MUX_BOT": return 0x06;
                    case "F8MUX_TOP": return 0x46;
                    case "F9MUX":     return 0x07;
                    // Versal
                    case "LOOKAHEAD8":  return 0x08;
                    case "CE1_IMR":     return 0x0D;
                    case "CE2_IMR":     return 0x2D;
                    case "CE3_IMR":     return 0x4D;
                    case "CE4_IMR":     return 0x6D;
                    case "WE_IMR":      return 0x7D;
                    case "SR_IMR":      return 0x0E;
                    case "FF_CLK_MOD":  return 0x0F;
                    case "IMR_CLK_MOD": return 0x4F;
                }
            } else if (tt == TileTypeEnum.BRAM) {
                // UltraScale+ BRAM
                switch (b.getBELType()) {
                    case "RAMBFIFO36E2_RAMBFIFO36E2": return 0;
                    case "RAMB36E2_RAMB36E2": return 0;
                    case "FIFO36E2_FIFO36E2": return 0;
                    case "RAMBFIFO18E2_RAMBFIFO18E2": return 1;
                    case "RAMB18E2_L_RAMB18E2": return 1;
                    case "FIFO18E2_FIFO18E2": return 1;
                    case "RAMB18E2_U_RAMB18E2": return 2;
                }
            } else if (tt == TileTypeEnum.BRAM_L || tt == TileTypeEnum.BRAM_R) {
                // 7-series BRAM
                boolean is_top18 = (st == SiteTypeEnum.RAMB18E1);
                switch(b.getBELType()) {
                    case "RAMBFIFO36E1_RAMBFIFO36E1": return 0;
                    case "RAMB36E1_RAMB36E1": return 0;
                    case "FIFO36E1_FIFO36E1": return 0;
                    case "RAMB18E1_RAMB18E1": return is_top18 ? 2 : 1;
                    case "FIFO18E1_FIFO18E1": return 1;
                }
            } else if (st == SiteTypeEnum.RAMB18_L || st == SiteTypeEnum.RAMB18_U || st == SiteTypeEnum.RAMB36) {
                // Versal BRAM
                switch (b.getBELType()) {
                    case "RAMB_RAMB36": return 0;
                    case "RAMB_RAMB18_L": return 1;
                    case "RAMB_RAMB18_U": return 2;
                }
            } else if (st == SiteTypeEnum.DSP58_PRIMARY || st == SiteTypeEnum.DSP58 || st == SiteTypeEnum.DSPFP || st == SiteTypeEnum.DSP48E2) {
                int site = 0;
                if (dx == 1 || dy == 1)
                    site |= (1 << 4);
                switch (b.getBELType()) {
                    // US+/Versal
                    case "DSP_PREADD_DATA": return site | 0;
                    case "DSP_PREADD": return site | 1;
                    case "DSP_A_B_DATA": return site | 2;
                    case "DSP_MULTIPLIER": return site | 3;
                    case "DSP_C_DATA": return site | 4;
                    case "DSP_M_DATA": return site | 5;
                    case "DSP_ALU": return site | 6;
                    case "DSP_OUTPUT": return site | 7;
                    // Versal
                    case "DSP_ALUADD": return site | 8;
                    case "DSP_ALUMUX": return site | 9;
                    case "DSP_ALUREG": return site | 10;
                    case "DSP_CAS_DELAY": return site | 11;
                    case "DSP_DFX": return site | 12;
                    case "DSP_PATDET": return site | 13;
                    // Versal FP
                    case "DSP_FP_ADDER": return site | 0;
                    case "DSP_FP_CAS_DELAY": return site | 1;
                    case "DSP_FP_INMUX": return site | 2;
                    case "DSP_FP_INREG": return site | 3;
                    case "DSP_FP_OUTPUT": return site | 4;
                    case "DSP_FPA_CREG": return site | 5;
                    case "DSP_FPA_OPM_REG": return site | 6;
                    case "DSP_FPM_PIPEREG": return site | 7;
                    case "DSP_FPM_STAGE0": return site | 8;
                    case "DSP_FPM_STAGE1": return site | 9;
                }
            } else if (st == SiteTypeEnum.DSP58_CPLX) {
                int site = (2 << 4);
                switch (b.getBELType()) {
                    case "DSP_CPLX_STAGE0": return site | 0;
                    case "DSP_CPLX_STAGE1": return site | 1;
                }
            }
            return -1;
        }

        private String get_bel_type(SiteInst s, BEL b) {
            SiteTypeEnum st = s.getSiteTypeEnum();
            String bt = b.getBELType();
            if (st == SiteTypeEnum.SLICEL || st == SiteTypeEnum.SLICEM) {
                // Use consistent type for LUT/FF bels in a SLICE
                if ((bt.endsWith("FF") || bt.endsWith("FF2")) && !bt.contains("IMI") && !bt.contains("IMC")) return "SLICE_FF";
                if (bt.endsWith("5LUT")) return st.toString() + "_5LUT";
                if (bt.endsWith("6LUT")) return st.toString() + "_6LUT";
                if (bt.equals("SLICEL_LUT5")) return "SLICEL_5LUT"; // consistency between Versal and xcup
                if (bt.equals("SLICEL_LUT6")) return "SLICEL_6LUT"; // consistency between Versal and xcup
                if (bt.equals("SLICEM_LUT5")) return "SLICEM_5LUT"; // consistency between Versal and xcup
                if (bt.equals("SLICEM_LUT6")) return "SLICEM_6LUT"; // consistency between Versal and xcup
            }
            if (bt.startsWith("PSS_ALTO_CORE_PAD"))
                return "PSS_ALTO_CORE_PAD";
            if (bt.startsWith("PSS_PAD_"))
                return "PSS_PAD";
            return bt;
        }

        private NextpnrBel add_bel(Design des, SiteInst s, int site_variant, BEL b) throws NoSuchAlgorithmException {
            if (b.getBELClass() == BELClass.PORT)
                return null;
            int z = bels.size(); // TODO: encoding validity info in Z ?
            int site_idx = s.getSite().getSiteIndexInTile();
            NextpnrBel nb = new NextpnrBel(b.getName(), bels.size(), get_bel_type(s, b), site_idx, site_variant, z);
            nb.place_idx = get_bel_place_idx(s, b);
            nb.placements = get_bel_placements(des, s, b);
            bels.add(nb);
            // Import bel pins
            for (BELPin bp : b.getPins()) {
                NextpnrBelWire nport = new NextpnrBelWire();
                nport.port_type = bp.isBidir() ? NextpnrPortDir.INOUT : (bp.isOutput() ? NextpnrPortDir.OUT : NextpnrPortDir.IN);
                nport.name = makeConstId(bp.getName());
                nport.wire = (bp.getSiteWireName() == null) ? -1 : site_wire_to_wire(s, site_variant, bp.getSiteWireName());
                if (nport.wire != -1)
                    wires.get(nport.wire).belpins.add(new NextpnrBelPinRef(nb.index, bp.getName()));
                nb.bel_ports.add(nport);
                // TODO: HARD0 bel constant routing?
            }
            if (b.getBELClass() == BELClass.RBEL)
                nb.flags |= BEL_FLAG_RBEL;
            if (pad_bels.contains(s.getSite().getSiteTypeEnum().toString() + "_" + b.getName()))
                nb.flags |= BEL_FLAG_PAD;
            bel_to_idx.put(site_idx + "_" + site_variant + "_" + b.getName(), nb.index);
            return nb;
        }

        // Add a PIP to the list of PIPs, and link it to its wires
        private NextpnrPip add_pip_base(NextpnrPip p) {
            pips.add(p);
            wires.get(p.src_wire).pips_dh.add(p.index);
            wires.get(p.dst_wire).pips_uh.add(p.index);
            return p;
        }

        private NextpnrPip add_site_pip(SiteInst s, int site_variant, SitePIP sp) {
            // Various PIPs we don't want in the database
            if (sp.getBELName().startsWith("TFBUSED"))
                return null;
            if (get_bel_type(s, sp.getBEL()).contains("LUT"))
                return null; // TODO: LUT route-throughs
            int site_idx = s.getSite().getSiteIndexInTile();
            int bel_idx = bel_to_idx.get(site_idx + "_" + site_variant + "_" + sp.getBELName());
            NextpnrPip np = new NextpnrPip(pips.size(),
                site_wire_to_wire(s, site_variant, sp.getInputPin().getSiteWireName()), // src
                site_wire_to_wire(s, site_variant, sp.getOutputPin().getSiteWireName()), // dst
                NextpnrPipType.SITE_INTERNAL,
                site_idx, site_variant,
                bel_idx, sp.getInputPin().getIndex(), sp.getOutputPin().getIndex()
            );
            BEL bel = sp.getBEL();
            if (bel.canInvert()) {
                if (bel.getInvertingPin().equals(sp.getInputPin())) {
                    if (bel.getNonInvertingPin().equals(sp.getInputPin())) {
                        np.flags |= PIP_FLAG_CAN_INV;
                    } else {
                        np.flags |= PIP_FLAG_FIXED_INV;
                    }
                }
            }
            return add_pip_base(np);
        }

        private NextpnrPip add_site_io_pip(SiteInst si, int site_variant, BELPin bp) {
            Site s = si.getSite();
            // Lookup primary pin name
            String site_pin = bp.getConnectedSitePinName();
            String prim_pin = si.getPrimarySitePinName(site_pin);
            if (prim_pin == null && s.getSiteTypeEnum() == si.getSiteTypeEnum())
                prim_pin = site_pin;
            // Lookup tile wire indices for site and tile side of port
            int site_side_wire = site_wire_to_wire(si, site_variant, bp.getSiteWireName());
            int tile_side_wire = wire_lookup.get(s.getTile().getWireIndex(s.getTileWireNameFromPinName(prim_pin)));
            // Create the entrance/exit PIPs
            NextpnrPip np = null;
            if (bp.isInput() || bp.isBidir()) {
                if (si.getSiteTypeEnum() == SiteTypeEnum.IPAD && site_pin.equals("O"))
                    return null;
                // TODO: ground driver pseudo-PIP for LUTs
                np = new NextpnrPip(pips.size(), site_side_wire, tile_side_wire,
                    NextpnrPipType.SITE_EXIT, s.getSiteIndexInTile(), site_variant, bp.getName());
            } else {
                // TODO: permutation PIPs for SLICEL/M LUT input pins
                np = new NextpnrPip(pips.size(), tile_side_wire, site_side_wire,
                    NextpnrPipType.SITE_ENTRANCE, s.getSiteIndexInTile(), site_variant, bp.getName());
            }
            return add_pip_base(np);
        }

        private NextpnrPip add_tile_pip(PIP p, boolean reverse) {
            int src_idx = wire_lookup.get(reverse ? p.getEndWireIndex() : p.getStartWireIndex());
            int dst_idx = wire_lookup.get(reverse ? p.getStartWireIndex() : p.getEndWireIndex());
            // FIXME: workaround for some pips/wires missing?
            if (src_idx == -1 || dst_idx == -1)
                return null;

            NextpnrPip np = new NextpnrPip(pips.size(), src_idx, dst_idx, NextpnrPipType.TILE_ROUTING);
            if (p.isRouteThru()) {
                np.flags |= PIP_FLAG_PSEUDO;
                // Find bel pins the pseudo pip passes through
                np.pp_pins = new ArrayList();
                PseudoPIPHelper pp = PseudoPIPHelper.getPseudoPIPHelper(p);
                Site target_site = null;
                // Find which site the pseudo pip is associated with
                SitePin start_pin = p.getStartWire().getSitePin(), end_pin = p.getEndWire().getSitePin();
                if (start_pin != null && start_pin.getSite().getSiteTypeEnum().equals(pp.getSiteTypeEnum()))
                    target_site = start_pin.getSite();
                else if (end_pin != null && end_pin.getSite().getSiteTypeEnum().equals(pp.getSiteTypeEnum()))
                    target_site = end_pin.getSite();
                if (target_site != null) {
                    // TODO: LUT route-through support
                    if (target_site.getSiteTypeEnum() == SiteTypeEnum.SLICEL || target_site.getSiteTypeEnum() == SiteTypeEnum.SLICEM)
                        return null;
                    List<BELPin> used_bel_pins = pp.getUsedBELPins();
                    if (used_bel_pins != null)
                        for (BELPin bp : pp.getUsedBELPins()) {
                            // TODO: would a route through that doesn't touch any other site bels care about these?
                            if (bp.getBEL().getBELClass() == BELClass.PORT)
                                continue;
                            // Lookup corresponding bel index
                            String bel_key = target_site.getSiteIndexInTile() + "_0_" + bp.getBEL().getName();
                            np.pp_pins.add(new NextpnrPseudoPipPin(bel_to_idx.get(bel_key), bp.getName()));
                        }
                }
            }
            if (reverse)
                np.flags |= PIP_FLAG_REVERSED;
            // TODO: extra tags for LUT route-through PIPs
            add_pip_base(np);
            return np;
        }

        private NextpnrPip add_synthetic_pip(int src_idx, int dst_idx, NextpnrPipType t, int flags) {
            NextpnrPip np = new NextpnrPip(pips.size(), src_idx, dst_idx, t);
            np.flags = flags;
            add_pip_base(np);
            return np;
        }

        private NextpnrBel add_synthetic_bel(String name, String type, String pin_name, int wire_idx) {
            NextpnrBel nb = new NextpnrBel(name, bels.size(), type, -1, 0, bels.size());
            NextpnrBelWire nport = new NextpnrBelWire();
            nport.port_type = NextpnrPortDir.OUT;
            nport.name = makeConstId(pin_name);
            nport.wire = wire_idx;
            wires.get(wire_idx).belpins.add(new NextpnrBelPinRef(nb.index, pin_name));
            nb.bel_ports.add(nport);
            bels.add(nb);
            return nb;
        }

        public NextpnrTileType(int index, TileTypeEnum tt) {
            this.index = index;
            this.type = makeConstId(tt.toString());
            this.bels = new ArrayList();
            this.wires = new ArrayList();
            this.pips = new ArrayList();
            this.sites = new ArrayList();

            bel_to_idx = new HashMap();
            site_wire_to_wire_idx = new HashMap();
            wire_lookup = flat_tiles.get(tt);
        }

        public void do_import(Design des, Tile t) throws NoSuchAlgorithmException {
            // Import wires, only including those with real connectivity
            for (int i = 0; i < wire_lookup.wire_to_flat.length; i++) {
                if (wire_lookup.wire_to_flat[i] == -1)
                    continue;
                assert(wires.size() == wire_lookup.wire_to_flat[i]);
                Wire w = new Wire(t, i);
                wires.add(new NextpnrWire(w.getWireName(), wires.size(), makeConstId(w.getIntentCode().toString())));
            }

            // Add a special wires for the constant pseudo-network
            row_gnd_wire_index = wires.size();
            wires.add(new NextpnrWire("PSEUDO_GND_WIRE_ROW", row_gnd_wire_index, makeConstId("PSEUDO_GND")));
            row_vcc_wire_index = wires.size();
            wires.add(new NextpnrWire("PSEUDO_VCC_WIRE_ROW", row_vcc_wire_index, makeConstId("PSEUDO_VCC")));
            global_gnd_wire_index = wires.size();
            wires.add(new NextpnrWire("PSEUDO_GND_WIRE_GLBL", global_gnd_wire_index, makeConstId("PSEUDO_GND")));
            global_vcc_wire_index = wires.size();
            wires.add(new NextpnrWire("PSEUDO_VCC_WIRE_GLBL", global_vcc_wire_index, makeConstId("PSEUDO_VCC")));

            tile_wire_count = wires.size();
            // Import site content
            int autoidx = 0;
            for (Site s : t.getSites()) {
                sites.add(new NextpnrTileTypeSite(t, s));
                // Determine variants
                ArrayList<SiteTypeEnum> variants = new ArrayList<>();
                variants.add(s.getSiteTypeEnum());
                variants.addAll(Arrays.asList(s.getAlternateSiteTypeEnums()));

                for (int variant = 0; variant < variants.size(); variant++) {
                    HashSet<BELPin> site_pins = new HashSet<>();
                    HashSet<SitePIP> site_pips = new HashSet<>();
                    SiteInst si = new SiteInst(t.getName() + "_" + (autoidx++), des, variants.get(variant), s);
                    for (BEL b : si.getBELs()) {
                        add_bel(des, si, variant, b);
                        if (b.getBELClass() == BELClass.PORT) {
                            for (BELPin bp : b.getPins()) {
                                String site_pin = bp.getConnectedSitePinName();
                                if (site_pin != null && !site_pins.contains(bp)) {
                                    site_pins.add(bp);
                                    add_site_io_pip(si, variant, bp);
                                }
                            }
                        }
                        for (BELPin bp : b.getPins())
                            site_pips.addAll(bp.getSitePIPs());
                    }
                    for (SitePIP sp : site_pips)
                        add_site_pip(si, variant, sp);
                    si.unPlace();
                }
            }
            for (PIP p : t.getPIPs()) {
                if (p.isRouteThru() && p.getStartWireName().endsWith("_CE_INT"))
                    continue; // these route through pips seem to cause antenna issues
                // TODO: LUT route-thru handling
                add_tile_pip(p, false);
                if (p.isBidirectional()) {
                    // nextpnr PIPs are always directional, for bidir ones create an anti-parallel pair
                    add_tile_pip(p, true);
                }
            }
            // Add synthetic const-driver bels
            if (t.getTileTypeEnum().equals(des.getDevice().getTile(0, 0).getTileTypeEnum())) {
                add_synthetic_bel("GND_DRIVER", "GND", "G", global_gnd_wire_index);
                add_synthetic_bel("VCC_DRIVER", "VCC", "P", global_vcc_wire_index);
            }
            add_synthetic_pip(global_gnd_wire_index, row_gnd_wire_index, NextpnrPipType.TILE_ROUTING, PIP_FLAG_SYNTHETIC);
            add_synthetic_pip(global_vcc_wire_index, row_vcc_wire_index, NextpnrPipType.TILE_ROUTING, PIP_FLAG_SYNTHETIC);
        }

        public void write_content_bba(PrintWriter bba) {
            for (NextpnrBel nb : bels) {
                bba.printf("label tt%d_b%d_wires\n", index, nb.index);
                for (NextpnrBelWire w : nb.bel_ports) {
                    bba.printf("u32 %d\n", w.name);
                    bba.printf("u32 %d\n", w.wire);
                    bba.printf("u32 %d\n", w.port_type.ordinal());
                }
                bba.printf("label tt%d_b%d_placements\n", index, nb.index);
                for (NextpnrBelCellMap p : nb.placements) {
                    bba.printf("u32 %d\n", p.cell_type);
                    bba.printf("u32 %d\n", p.pin_map_idx);
                }
            }
            bba.printf("label tt%d_bels\n", index);
            for (NextpnrBel nb : bels) {
                bba.printf("u32 %d\n", nb.name);
                bba.printf("u32 %d\n", nb.bel_type);
                bba.printf("u16 %d\n", nb.site);
                bba.printf("u16 %d\n", nb.site_variant);
                bba.printf("u32 %d\n", nb.flags);
                bba.printf("u32 %d\n", nb.z);
                bba.printf("u32 %d\n", nb.place_idx);
                bba.printf("ref tt%d_b%d_wires\n", index, nb.index);
                bba.printf("u32 %d\n", nb.bel_ports.size());
                bba.printf("ref tt%d_b%d_placements\n", index, nb.index);
                bba.printf("u32 %d\n", nb.placements.size());
            }

            for (NextpnrWire nw : wires) {
                bba.printf("label tt%d_w%d_uh\n", index, nw.index);
                for (int uh : nw.pips_uh)
                    bba.printf("u32 %d\n", uh);
                bba.printf("label tt%d_w%d_dh\n", index, nw.index);
                for (int dh : nw.pips_dh)
                    bba.printf("u32 %d\n", dh);
                bba.printf("label tt%d_w%d_pins\n", index, nw.index);
                for (NextpnrBelPinRef bp : nw.belpins) {
                    bba.printf("u32 %d\n", bp.bel);
                    bba.printf("u32 %d\n", bp.port);
                }
            }
            bba.printf("label tt%d_wires\n", index);
            for (NextpnrWire nw : wires) {
                bba.printf("u32 %d\n", nw.name);
                bba.printf("u16 %d\n", nw.site);
                bba.printf("u16 %d\n", nw.site_variant);
                bba.printf("u32 %d\n", nw.intent);
                bba.printf("u32 %d\n", nw.flags);
                bba.printf("ref tt%d_w%d_uh\n", index, nw.index);
                bba.printf("u32 %d\n", nw.pips_uh.size());
                bba.printf("ref tt%d_w%d_dh\n", index, nw.index);
                bba.printf("u32 %d\n", nw.pips_dh.size());
                bba.printf("ref tt%d_w%d_pins\n", index, nw.index);
                bba.printf("u32 %d\n", nw.belpins.size());
            }
            for (NextpnrPip np : pips) {
                if (np.pp_pins == null)
                    continue;
                bba.printf("label tt%d_p%d_ppins\n", index, np.index);
                for (NextpnrPseudoPipPin ppp : np.pp_pins) {
                    bba.printf("u32 %d\n", ppp.bel_index);
                    bba.printf("u32 %d\n", ppp.pin_name);
                }
            }
            bba.printf("label tt%d_pips\n", index);
            for (NextpnrPip np : pips) {
                bba.printf("u32 %d\n", np.src_wire);
                bba.printf("u32 %d\n", np.dst_wire);
                bba.printf("u16 %d\n", np.type.ordinal());
                bba.printf("u16 %d\n", np.flags);
                bba.printf("u16 %d\n", np.site);
                bba.printf("u16 %d\n", np.site_variant);
                if (np.type == NextpnrPipType.SITE_ENTRANCE || np.type == NextpnrPipType.SITE_EXIT) {
                    bba.printf("u32 %d\n", np.site_port);
                    bba.printf("u32 %d\n", 0);
                } else if (np.type == NextpnrPipType.TILE_ROUTING && ((np.flags & PIP_FLAG_PSEUDO) != 0)) {
                    bba.printf("ref tt%d_p%d_ppins\n", index, np.index);
                    bba.printf("u32 %d\n", np.pp_pins.size());
                } else {
                    bba.printf("u16 %d\n", np.bel);
                    bba.printf("u16 %d\n", np.from_pin);
                    bba.printf("u16 %d\n", np.to_pin);
                    bba.printf("u16 %d\n", 0);
                }
            }
            for (NextpnrTileTypeSite ns : sites) {
                bba.printf("label tt%d_s%d_variants\n", index, ns.index);
                for (int vt : ns.variant_types) {
                    bba.printf("u32 %d\n", vt);
                }
            }
            bba.printf("label tt%d_sites\n", index);
            for (NextpnrTileTypeSite ns : sites) {
                bba.printf("u32 %d\n", ns.prefix);
                bba.printf("ref tt%d_s%d_variants\n", index, ns.index);
                bba.printf("u32 %d\n", ns.variant_types.size());
                bba.printf("u16 %d\n", ns.dx);
                bba.printf("u16 %d\n", ns.dy);
            }
        }

        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", type);
            bba.printf("ref tt%d_bels\n", index);
            bba.printf("u32 %d\n", bels.size());
            bba.printf("ref tt%d_wires\n", index);
            bba.printf("u32 %d\n", wires.size());
            bba.printf("ref tt%d_pips\n", index);
            bba.printf("u32 %d\n", pips.size());
            bba.printf("ref tt%d_sites\n", index);
            bba.printf("u32 %d\n", sites.size());
        }

    }


    static class NextpnrSiteInst {
        public NextpnrSiteInst(Site s) {
            this.index = s.getSiteIndexInTile();
            this.prefix = makeConstId(s.getNameSpacePrefix());
            this.site_x = s.getInstanceX();
            this.site_y = s.getInstanceY();
            this.inter_x = -1;
            this.inter_y = -1;
            try {
                Tile int_t = s.getIntTile();
                if (int_t != null) {
                    this.inter_x = int_t.getColumn();
                    this.inter_y = int_t.getRow();
                }
            } catch (java.lang.NullPointerException e) {
            } catch (java.lang.IndexOutOfBoundsException e) {
            }
        }
        int index;
        int prefix;
        int site_x, site_y;
        int inter_x, inter_y;
    }

    static class NextpnrTileInst {
        int index;
        int type_idx; // indexes a NextpnrTileType for the tile (a Xilinx tile type)
        int shape_idx; // indexes a DedupTileInst for the tile (a nextpnr-xlnx deduplicated tile shape)
        int name_prefix;
        int tile_x, tile_y;
        int clock_x, clock_y;
        int slr;
        ArrayList<NextpnrSiteInst> sites;
        public NextpnrTileInst(Tile t, int shape_idx) {
            this.index = t.getUniqueAddress();
            this.type_idx = tile_type_idx.get(t.getTileTypeEnum());
            this.shape_idx = shape_idx;
            this.name_prefix = makeConstId(t.getNameRoot());
            this.tile_x = t.getTileXCoordinate();
            this.tile_y = t.getTileYCoordinate();
            ClockRegion clkr = t.getClockRegion();
            if (clkr == null) {
                this.clock_x = -1;
                this.clock_y = -1;
            } else {
                this.clock_x = clkr.getInstanceX();
                this.clock_y = clkr.getInstanceY();
            }
            this.slr = t.getSLR().getId();
            this.sites = new ArrayList();
            for (Site s : t.getSites()) {
                this.sites.add(new NextpnrSiteInst(s));
            }
        }

        public void write_content_bba(PrintWriter bba) {
            bba.printf("label ti%d_sites\n", index);
            for (NextpnrSiteInst nsi : sites) {
                bba.printf("u32 %d\n", nsi.prefix);
                bba.printf("u16 %d\n", nsi.site_x);
                bba.printf("u16 %d\n", nsi.site_y);
                bba.printf("u16 %d\n", nsi.inter_x);
                bba.printf("u16 %d\n", nsi.inter_y);
            }
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", type_idx);
            bba.printf("u32 %d\n", shape_idx);
            bba.printf("u32 %d\n", name_prefix);
            bba.printf("u16 %d\n", tile_x);
            bba.printf("u16 %d\n", tile_y);
            bba.printf("u16 %d\n", clock_x);
            bba.printf("u16 %d\n", clock_y);
            bba.printf("u16 %d\n", slr);
            bba.printf("u16 %d\n", 0); // padding
            bba.printf("ref ti%d_sites\n", index);
            bba.printf("u32 %d\n", sites.size());
        }
    }

    // Returns true if a wire is not useful
    public static boolean is_dead_wire(Wire w) {
        return w.getBackwardPIPs().size() == 0 && w.getForwardPIPs().size() == 0 && w.getSitePin() == null;
    }

    public static class DedupTileFlatWires {
        // This squishes out wires with no associated pips or site pins from a tile
        public int [] wire_to_flat;
        public int active_count;
        public DedupTileFlatWires(Tile t) {
            int flat_idx = 0;
            wire_to_flat = new int[t.getWireCount()];
            for (int i = 0; i < t.getWireCount(); i++) {
                Wire w = new Wire(t, i);
                if (is_dead_wire(w)) {
                    wire_to_flat[i] = -1;
                } else {
                    wire_to_flat[i] = flat_idx;
                    ++flat_idx;
                }
            }
            active_count = flat_idx;
        }
        int get(int w) {
            return wire_to_flat[w];
        }
        int size() {
            return active_count;
        }
    }

    // Keep track of all tied-to-ground and tied-to-Vcc wires in the current row, so we can combine them into a single node
    private static ArrayList<Wire> row_vcc = new ArrayList();
    private static ArrayList<Wire> row_gnd = new ArrayList();

    // Determine if a node is a useful ground/vcc source (TODO: determine if all these exceptions are still valid)
    public static boolean is_vcc(Node n) {
        if (!n.isTiedToVcc())
            return false;
        Tile t = n.getTile();
        return (t.getTileTypeEnum() != TileTypeEnum.BRAM_INT_INTERFACE_L && t.getTileTypeEnum() != TileTypeEnum.BRAM_INT_INTERFACE_R);
    }

    public static boolean is_gnd(Node n) {
        if (!n.isTiedToGnd())
            return false;
        Tile t = n.getTile();
        return (t.getTileTypeEnum() != TileTypeEnum.BRAM_INT_INTERFACE_L && t.getTileTypeEnum() != TileTypeEnum.BRAM_INT_INTERFACE_R
                         && t.getTileTypeEnum() != TileTypeEnum.RCLK_INT_L && t.getTileTypeEnum() != TileTypeEnum.RCLK_INT_R);
    }

    // Representation of a shape of a node using relative coordinates
    public static class NodeShape {
        public int [][] node_wires;
        // From a Xilinx node
        public NodeShape(Tile base, Node n) {
            Wire [] nw = n.getAllWiresInNode();
            int live_wire_count = 0;
            for (Wire w : nw)
                if (!is_dead_wire(w))
                    ++live_wire_count;
            node_wires = new int[live_wire_count][3];
            int j = 0;
            for (Wire w : nw) {
                if (is_dead_wire(w))
                    continue;
                Tile tw = w.getTile();
                node_wires[j][0] = tw.getColumn() - base.getColumn();
                node_wires[j][1] = tw.getRow() - base.getRow();
                node_wires[j][2] = flat_tiles.get(tw.getTileTypeEnum()).get(w.getWireIndex());
                ++j;
            }
        }
        // For row/global constants
        public NodeShape(Device d, int row, boolean is_vcc, boolean is_global) {
            int wire_count;
            if (is_global)
                wire_count = d.getRows();
            else
                wire_count = d.getColumns() + (is_vcc ? row_vcc.size() : row_gnd.size());
            node_wires = new int[wire_count][3];
            for (int i = 0; i < (is_global ? d.getRows() : d.getColumns()); i++) {
                node_wires[i][0] = (is_global ? 0 : i);
                node_wires[i][1] = (is_global ? i : 0);
                node_wires[i][2] = flat_tiles.get(d.getTile(is_global ? i : row, is_global ? 0 : i).getTileTypeEnum()).size() +
                    (is_global ? 2 : 0) + (is_vcc ? 1 : 0);
            }
            if (!is_global) {
                int j = d.getColumns();
                for (Wire w : (is_vcc ? row_vcc : row_gnd)) {
                    Tile tw = w.getTile();
                    node_wires[j][0] = tw.getColumn();
                    node_wires[j][1] = tw.getRow() - row;
                    node_wires[j][2] = flat_tiles.get(tw.getTileTypeEnum()).get(w.getWireIndex());
                    ++j;
                }
            }
        }

        public String hash() throws NoSuchAlgorithmException {
            MessageDigest md = MessageDigest.getInstance("md5");
            byte[] tempbuf = {0, 0, 0, 0};
            java.util.function.Consumer<Integer> add_int = (Integer x) -> {
                tempbuf[0] = (byte)(x >> 24);
                tempbuf[1] = (byte)(x >> 16);
                tempbuf[2] = (byte)(x >> 8);
                tempbuf[3] = (byte)(x >> 0);
                md.update(tempbuf);
            };
            add_int.accept(node_wires.length);
            for (int [] w : node_wires) {
                add_int.accept(w[0]);
                add_int.accept(w[1]);
                add_int.accept(w[2]);
            }
            byte[] digest = md.digest();
            return Base64.getEncoder().encodeToString(digest);
        }

        public void write_content_bba(int index, PrintWriter bba) {
            boolean misaligned = false;
            bba.printf("label ns%d_wires\n", index);
            for (int [] w : node_wires) {
                bba.printf("u16 %d\n", w[0]);
                bba.printf("u16 %d\n", w[1]);
                bba.printf("u16 %d\n", w[2]);
                misaligned = !misaligned;
            }
            if (misaligned)
                bba.printf("u16 0\n"); // align to 32 bits
        }
        public void write_bba(int index, PrintWriter bba) {
            bba.printf("ref ns%d_wires\n", index);
            bba.printf("u32 %d\n", node_wires.length);
        }
    }

    private static final int MODE_TILE_WIRE = 0x7000;
    private static final int MODE_IS_ROOT = 0x7001;
    private static final int MODE_ROW_CONST = 0x7002;
    private static final int MODE_GLB_CONST = 0x7003;

    // The set of all the different node shapes
    private static ArrayList<NodeShape> node_shapes = new ArrayList<>();
    private static HashMap<String, Integer> node_shapes_by_hash = new HashMap();

    // Gets the deduplicated index of a node shape; adding it and returning a new index if it's not been seen before
    public static int node_shape_idx(NodeShape shape) throws NoSuchAlgorithmException {
        String hash = shape.hash();
        if (node_shapes_by_hash.containsKey(hash))
            return node_shapes_by_hash.get(hash);
        int idx = node_shapes.size();
        node_shapes_by_hash.put(hash, idx);
        node_shapes.add(shape);
        return idx;
    }

    public static Wire get_node_root(Node n) {
        if (n == null)
            return null;
        // We define the root of a node to be the first live wire in it, because we might have pruned the real root
        // TODO: determine the real value of pruning against consistent matching of Vivado wire names
        // and the general cost of doing this
        for (Wire w : n.getAllWiresInNode()) {
            if (!is_dead_wire(w))
                return w;
        }
        return null;
    }

    public static class DedupTileInst {
        public int [][] wire_to_node;
        public int [][][] node_wires;
        public String hash_value = null;

        public DedupTileInst(Device d, Tile t) throws NoSuchAlgorithmException {
            DedupTileFlatWires fw = flat_tiles.get(t.getTileTypeEnum());
            wire_to_node = new int[fw.size() + 4][3];
            int const_tile_offset = flat_tiles.get(d.getTile(t.getRow(), 0).getTileTypeEnum()).size();
            // Xilinx wires
            for (int wi = 0; wi < t.getWireCount(); wi++) {
                int i = fw.get(wi);
                if (i == -1)
                    continue;
                Wire w = new Wire(t, wi);
                Node n = w.getNode();
                Wire root = get_node_root(n);

                if (n == null) {
                    wire_to_node[i][0] = MODE_TILE_WIRE;
                    wire_to_node[i][1] = 0;
                    wire_to_node[i][2] = 0;
                } else if (is_gnd(n)) {
                    wire_to_node[i][0] = MODE_ROW_CONST;
                    wire_to_node[i][1] = 0;
                    wire_to_node[i][2] = const_tile_offset + 0;
                    row_gnd.add(w);
                } else if (is_vcc(n)) {
                    wire_to_node[i][0] = MODE_ROW_CONST;
                    wire_to_node[i][1] = 0;
                    wire_to_node[i][2] = const_tile_offset + 1;
                    row_vcc.add(w);
                } else if (w.equals(root)) {
                    // Is a root
                    wire_to_node[i][0] = MODE_IS_ROOT;
                    wire_to_node[i][1] = node_shape_idx(new NodeShape(t, n));
                    wire_to_node[i][2] = 0;
                } else {
                    Tile tn = root.getTile();
                    wire_to_node[i][0] = tn.getColumn() - t.getColumn();
                    wire_to_node[i][1] = tn.getRow() - t.getRow();
                    wire_to_node[i][2] = flat_tiles.get(tn.getTileTypeEnum()).get(root.getWireIndex());
                }
            }
            // Synthetic constant nodes
            for (int j = 0; j < 4; j++) {
                int i = j + fw.size();
                if ((t.getColumn() == 0) && ((j < 2) || (t.getRow() == 0))) {
                    // Root of row or global const
                    wire_to_node[i][0] = MODE_IS_ROOT;
                    wire_to_node[i][1] = node_shape_idx(new NodeShape(d, t.getRow(), (j == 1) || (j == 3), (j >= 2)));
                    wire_to_node[i][2] = 0;
                } else if ((j < 2) || (t.getColumn() == 0)) {
                    // Back reference to row/global origin
                    int offset = (j < 2) ? const_tile_offset : flat_tiles.get(d.getTile(0, 0).getTileTypeEnum()).size();
                    wire_to_node[i][0] = (j < 2) ? MODE_ROW_CONST : MODE_GLB_CONST;
                    wire_to_node[i][1] = 0;
                    wire_to_node[i][2] = offset + j;
                } else {
                    // Not part of a node (global ground wire in a column other than zero)
                    wire_to_node[i][0] = MODE_TILE_WIRE;
                    wire_to_node[i][1] = 0;
                    wire_to_node[i][2] = 0;
                }
            }
        }


        public String hash() throws NoSuchAlgorithmException {
            if (hash_value == null) {
                MessageDigest md = MessageDigest.getInstance("md5");
                byte[] tempbuf = {0, 0, 0, 0};
                java.util.function.Consumer<Integer> add_int = (Integer x) -> {
                    tempbuf[0] = (byte) (x >> 24);
                    tempbuf[1] = (byte) (x >> 16);
                    tempbuf[2] = (byte) (x >> 8);
                    tempbuf[3] = (byte) (x >> 0);
                    md.update(tempbuf);
                };
                add_int.accept(wire_to_node.length);
                for (int[] entry : wire_to_node) {
                    add_int.accept(entry[0]); // dx/mode
                    add_int.accept(entry[1]); // dy/shape index
                    add_int.accept(entry[2]); // wire index
                }
                byte[] digest = md.digest();
                hash_value = Base64.getEncoder().encodeToString(digest);
            }
            return hash_value;
        }


        public void write_content_bba(int index, PrintWriter bba) {
            boolean misaligned = false;
            bba.printf("label ts%d_w2n\n", index);
            for (int [] entry : wire_to_node) {
                bba.printf("u16 %d\n", entry[0]);
                if (entry[0] == MODE_IS_ROOT) {
                    // 32-bit node shape index split across two values
                    bba.printf("u16 %d\n", (entry[1] & 0xFFFF));
                    bba.printf("u16 %d\n", ((entry[1] >> 16) & 0xFFFF));
                } else {
                    bba.printf("u16 %d\n", entry[1]);
                    bba.printf("u16 %d\n", entry[2]);
                }
                misaligned = !misaligned;
            }
            if (misaligned)
                bba.printf("u16 0\n"); // align to 32 bits
        }
        public void write_bba(int index, PrintWriter bba) {
            bba.printf("ref ts%d_w2n\n", index);
            bba.printf("u32 %d\n", wire_to_node.length);
        }
    }

    public static class NextpnrMacroCellInst {
        int index;
        int name;
        int type;
        ArrayList<NextpnrParam> params;

        public NextpnrMacroCellInst(int index, EDIFCellInst cell) {
            this.index = index;
            this.name = makeConstId(cell.getName());
            this.type = makeConstId(cell.getCellType().getName());
            this.params = new ArrayList();
            for (Map.Entry<String, EDIFPropertyValue> prop : cell.getPropertiesMap().entrySet()) {
                this.params.add(new NextpnrParam(prop.getKey(), prop.getValue()));
            }
        }
        public void write_content_bba(PrintWriter bba) {
            bba.printf("label mc%d_params\n", index);
            for (NextpnrParam p : params)
                p.write_bba(bba);
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", name);
            bba.printf("u32 %d\n", type);
            bba.printf("ref mc%d_params\n", index);
            bba.printf("u32 %d\n", params.size());
        }
    }

    private static NextpnrPortDir convert_direction(EDIFDirection dir) {
        switch (dir) {
            case INPUT:
                return NextpnrPortDir.IN;
            case OUTPUT:
                return NextpnrPortDir.OUT;
            case INOUT:
                return NextpnrPortDir.INOUT;
        }
        return NextpnrPortDir.INOUT;
    }

    public static class NextpnrMacroPortInst {
        int instance;
        int port;
        NextpnrPortDir dir;

        public NextpnrMacroPortInst(EDIFPortInst port) {
            if (port.isTopLevelPort())
                this.instance = 0;
            else
                this.instance = makeConstId(port.getCellInst().getName());
            this.port = makeConstId(port.getName());
            this.dir = convert_direction(port.getDirection());
        }

        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", instance);
            bba.printf("u32 %d\n", port);
            bba.printf("u32 %d\n", dir.ordinal());
        }
    }

    public static class NextpnrMacroNet {
        int index;
        int name;
        ArrayList<NextpnrMacroPortInst> ports;

        public NextpnrMacroNet(int index, EDIFNet net) {
            this.index = index;
            this.name = makeConstId(net.getName());
            this.ports = new ArrayList();
            for (EDIFPortInst port : net.getPortInsts())
                this.ports.add(new NextpnrMacroPortInst(port));
        }
        public void write_content_bba(PrintWriter bba) {
            bba.printf("label mn%d_ports\n", index);
            for (NextpnrMacroPortInst p : ports)
                p.write_bba(bba);
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", name);
            bba.printf("ref mn%d_ports\n", index);
            bba.printf("u32 %d\n", ports.size());
        }
    }

    private static int macro_obj_idx = 0;

    public static class NextpnrMacro {
        int name;
        ArrayList<NextpnrMacroCellInst> cells;
        ArrayList<NextpnrMacroNet> nets;

        public NextpnrMacro(EDIFCell cell) {
            this.name = makeConstId(cell.getName());
            this.cells = new ArrayList();
            this.nets = new ArrayList();
            for (EDIFCellInst eci : cell.getCellInsts()) {
                this.cells.add(new NextpnrMacroCellInst(macro_obj_idx, eci));
                macro_obj_idx += 1;
            }
            for (EDIFNet en : cell.getNets()) {
                this.nets.add(new NextpnrMacroNet(macro_obj_idx, en));
                macro_obj_idx += 1;
            }
        }
        public void write_content_bba(PrintWriter bba) {
            for (NextpnrMacroCellInst c : cells)
                c.write_content_bba(bba);
            for (NextpnrMacroNet n : nets)
                n.write_content_bba(bba);

            bba.printf("label m%d_cells\n", name);
            for (NextpnrMacroCellInst c : cells)
                c.write_bba(bba);
            bba.printf("label m%d_nets\n", name);
            for (NextpnrMacroNet n : nets)
                n.write_bba(bba);
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", name);
            bba.printf("ref m%d_cells\n", name);
            bba.printf("u32 %d\n", cells.size());
            bba.printf("ref m%d_nets\n", name);
            bba.printf("u32 %d\n", nets.size());
        }
    }

    public static class NextpnrPinDefault {
        public int name;
        public int value;
        public NextpnrPinDefault(String name, int value) {
            this.name = makeConstId(name);
            this.value = value;
        }
    }

    public static class NextpnrPinInversion {
        public int name;
        public int param;
        public NextpnrPinInversion(String name, String param) {
            this.name = makeConstId(name);
            this.param = makeConstId(param);
        }
    }

    public static class NextpnrLogicalPort {
        public int name;
        public NextpnrPortDir dir;
        public int bus_start;
        public int bus_end;
        public NextpnrLogicalPort(EDIFPort port) {
            this.name = makeConstId(port.getBusName());
            this.dir = convert_direction(port.getDirection());
            if (port.isBus()) {
                this.bus_start = 0; // TODO: ever non-zero for any Xilinx prim?
                this.bus_end = port.getWidth() - 1;
            } else {
                this.bus_start = -1;
                this.bus_end = -1;
            }
        }
    }

    private static int parse_verilog_width(String prop_value) {
        return Integer.parseInt(prop_value.substring(0, prop_value.indexOf("'")));
    }
    private static int PARAM_FMT_STRING = 0;
    private static int PARAM_FMT_BOOLEAN = 1;
    private static int PARAM_FMT_INTEGER = 2;
    private static int PARAM_FMT_FLOAT = 3;
    private static int PARAM_FMT_VBIN = 4;
    private static int PARAM_FMT_VHEX = 5;

    public static class NextpnrCellParameter {
        public int name;
        public int format;
        public int default_value;
        public int width;

        public NextpnrCellParameter(String name, VivadoProp prop) {
            this.name = makeConstId(name);
            this.default_value = makeConstId(prop.getValue());
            this.width = -1;
            switch (prop.getType()) {
                case BINARY:
                    this.format = PARAM_FMT_VBIN;
                    this.width = parse_verilog_width(prop.getValue());
                    break;
                case BOOL:
                    this.format = PARAM_FMT_BOOLEAN;
                    this.width = 1;
                    break;
                case DOUBLE:
                    this.format = PARAM_FMT_FLOAT;
                    break;
                case HEX:
                    this.format = PARAM_FMT_VHEX;
                    this.width = parse_verilog_width(prop.getValue());
                    break;
                case INT:
                    this.format = PARAM_FMT_INTEGER;
                    break;
                case STRING:
                    if (prop.getValue().contains("'b")) {
                        this.format = PARAM_FMT_VBIN;
                        this.width = parse_verilog_width(prop.getValue());
                    } else if (prop.getValue().contains("'h")) {
                        this.format = PARAM_FMT_VHEX;
                        this.width = parse_verilog_width(prop.getValue());
                    } else {
                        this.format = PARAM_FMT_STRING;
                    }
                    break;
                default:
                    assert(false);
            }
        }
    }

    public static class NextpnrCellType {
        public int cell_type;
        public int library;
        public ArrayList<NextpnrPinDefault> defaults;
        public ArrayList<NextpnrPinInversion> inversions;
        public ArrayList<NextpnrLogicalPort> logical_ports;
        public ArrayList<NextpnrCellParameter> parameters;

        public NextpnrCellType(Device dev, EDIFCell cell, String library) {
            this.cell_type = makeConstId(cell.getName());
            this.library = makeConstId(library);
            this.defaults = new ArrayList();
            this.inversions = new ArrayList();
            this.logical_ports = new ArrayList();
            this.parameters = new ArrayList();
            for (EDIFPort p : cell.getPorts())
                this.logical_ports.add(new NextpnrLogicalPort(p));
            Map<String, VivadoProp> default_props = Design.getDefaultCellProperties(dev.getSeries(), cell.getName());
            if (default_props != null)
                for (Map.Entry<String, VivadoProp> prop : default_props.entrySet()) {
                    this.parameters.add(new NextpnrCellParameter(prop.getKey(), prop.getValue()));
                }
        }

        public void write_content_bba(PrintWriter bba) {
            bba.printf("label ct%d_defaults\n", cell_type);
            for (NextpnrPinDefault d : defaults) {
                bba.printf("u32 %d\n", d.name);
                bba.printf("u32 %d\n", d.value);
            }
            bba.printf("label ct%d_inversions\n", cell_type);
            for (NextpnrPinInversion i : inversions) {
                bba.printf("u32 %d\n", i.name);
                bba.printf("u32 %d\n", i.param);
            }
            bba.printf("label ct%d_log_ports\n", cell_type);
            for (NextpnrLogicalPort p : logical_ports) {
                bba.printf("u32 %d\n", p.name);
                bba.printf("u32 %d\n", p.dir.ordinal());
                bba.printf("u32 %d\n", p.bus_start);
                bba.printf("u32 %d\n", p.bus_end);
            }
            bba.printf("label ct%d_parameters\n", cell_type);
            for (NextpnrCellParameter p : parameters) {
                bba.printf("u32 %d\n", p.name);
                bba.printf("u32 %d\n", p.format);
                bba.printf("u32 %d\n", p.default_value);
                bba.printf("u32 %d\n", p.width);
            }
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", cell_type);
            bba.printf("u32 %d\n", library);
            bba.printf("ref ct%d_defaults\n", cell_type);
            bba.printf("u32 %d\n", defaults.size());
            bba.printf("ref ct%d_inversions\n", cell_type);
            bba.printf("u32 %d\n", inversions.size());
            bba.printf("ref ct%d_log_ports\n", cell_type);
            bba.printf("u32 %d\n", logical_ports.size());
            bba.printf("ref ct%d_parameters\n", cell_type);
            bba.printf("u32 %d\n", parameters.size());
        }
    }

    private static void add_cell_types_from_lib(ArrayList<NextpnrCellType> result, Design des, Map<Unisim, Map<String, NetType>> pin_defaults, EDIFLibrary lib, String lib_name) {
        Device dev = des.getDevice();
        for (EDIFCell prim : lib.getCells()) {
            String type = prim.getName();
            Unisim unisim = null;
            try {
                unisim = Unisim.valueOf(type);
            } catch (java.lang.IllegalArgumentException e) {
                continue;
            }
            NextpnrCellType ct = new NextpnrCellType(dev, prim, lib_name);
            Map<String,String> inv_pins = DesignTools.getInvertiblePinMap(dev.getSeries(), unisim);

            if (pin_defaults.containsKey(unisim)) {
                for (Map.Entry<String, NetType> entry : pin_defaults.get(unisim).entrySet()) {
                    int value = (entry.getValue() == NetType.GND) ? 0 : ((entry.getValue() == NetType.VCC) ? 1 : 2);
                    ct.defaults.add(new NextpnrPinDefault(entry.getKey(), value));
                }
            }

            for(Map.Entry<String, String> entry : inv_pins.entrySet()) {
                ct.inversions.add(new NextpnrPinInversion(entry.getKey(), entry.getValue()));
            }
            result.add(ct);
        }
    }

    public static ArrayList<NextpnrCellType> get_cell_types(Design des) {
        Device dev = des.getDevice();
        ArrayList<NextpnrCellType> result = new ArrayList();

        Map<Unisim, Map<String, NetType>> pin_defaults = CellPinStaticDefaults.getCellPinDefaultsMap().get(dev.getSeries());
        add_cell_types_from_lib(result, des, pin_defaults, Design.getPrimitivesLibrary(dev.getName()), "primitives");
        add_cell_types_from_lib(result, des, pin_defaults, Design.getMacroPrimitives(dev.getSeries()), "macros");
        return result;
    }

    public static class NextpnrPad {
        int package_pin;
        int tile_idx;
        int site_idx;
        int bel_name;
        int site_type_name;
        int pad_function;
        int pad_complement;
        int pad_bank;
        int flags;

        static final int DIFF_SIG = 0x0001;
        static final int GENERAL_PURPOSE = 0x0002;
        static final int GLOBAL_CLK = 0x0004;
        static final int LOW_CAP = 0x0008;
        static final int VREF = 0x0010;
        static final int VRN = 0x0020;
        static final int VRP = 0x0040;

        NextpnrPad(Device d, PackagePin p) {
            this.package_pin = makeConstId(p.getName());
            if (p.getSite() != null) {
                Tile t = p.getSite().getTile();
                this.tile_idx = t.getRow() * d.getColumns() + t.getColumn();
                this.site_idx = p.getSite().getSiteIndexInTile();
                this.site_type_name = makeConstId(p.getSiteTypeEnum().toString());
            } else {
                this.tile_idx = -1;
                this.site_idx = -1;
                this.site_type_name = 0;
            }
            if (p.getBEL() != null)
                this.bel_name = makeConstId(p.getBEL().getName());
            else
                this.bel_name = 0;
            this.pad_function = makeConstId(p.getPinFunction());
            this.pad_complement = -1;
            this.pad_bank = -1;
            if (p.getIOBank() != null)
                this.pad_bank = makeConstId(p.getIOBank().toString());
            this.flags = 0;
            if (p.isDifferential()) this.flags |= DIFF_SIG;
            if (p.isGeneralPurpose()) this.flags |= GENERAL_PURPOSE;
            if (p.isGlobalClk()) this.flags |= GLOBAL_CLK;
            if (p.isLowCap()) this.flags |= LOW_CAP;
            if (p.isVref()) this.flags |= VREF;
            if (p.isVrn()) this.flags |= VRN;
            if (p.isVrp()) this.flags |= VRP;
            if (p.getBEL() != null)
                pad_bels.add(p.getSiteTypeEnum().toString() + "_" + p.getBEL().getName());
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", package_pin);
            bba.printf("u32 %d\n", tile_idx);
            bba.printf("u32 %d\n", site_idx);
            bba.printf("u32 %d\n", bel_name);
            bba.printf("u32 %d\n", site_type_name);
            bba.printf("u32 %d\n", pad_function);
            bba.printf("u32 %d\n", pad_complement);
            bba.printf("u32 %d\n", pad_bank);
            bba.printf("u32 %d\n", flags);
        }
    }

    public static class NextpnrPackage {
        int name;
        ArrayList<NextpnrPad> pads;
        NextpnrPackage(Device d, Package p) {
            this.name = makeConstId(p.getName());
            this.pads = new ArrayList();
            HashMap<String, Integer> pin2idx = new HashMap();
            for (PackagePin pin : p.getPackagePinMap().values()) {
                pin2idx.put(pin.getName(), this.pads.size());
                this.pads.add(new NextpnrPad(d, pin));
            }
            // Set up differential pin indices
            for (PackagePin pin : p.getPackagePinMap().values()) {
                PackagePin diff = pin.getDiffPairPin();
                if (diff == null)
                    continue;
                this.pads.get(pin2idx.get(pin.getName())).pad_complement = pin2idx.get(diff.getName());
            }
        }

        public void write_content_bba(PrintWriter bba) {
            bba.printf("label pkg%d_pads\n", name);
            for (NextpnrPad p : pads) {
                p.write_bba(bba);
            }
        }
        public void write_bba(PrintWriter bba) {
            bba.printf("u32 %d\n", name);
            bba.printf("ref pkg%d_pads\n", name);
            bba.printf("u32 %d\n", pads.size());
        }
    }

    private static final int chipdb_version = 1;

    public static void main(String[] args) throws NoSuchAlgorithmException, IOException {
        if (args.length < 3) {
            System.err.println("Usage: bbaexport <device> <constids.inc> <output.bba>");
            System.err.println("   e.g bbaexport xczu2cg-sbva484-1-e ./rapidwright/constids.inc ./rapidwright/xczu2cg.bba");
            System.err.println("   Use bbasm to convert bba to bin for nextpnr");
            System.exit(1);
        }
        // Init constids
        makeConstId(""); // id 0 is the empty string
        int known_id_count = 1;
        Scanner scanner = new Scanner(new File(args[1]));
        while (scanner.hasNextLine()) {
            String nl = scanner.nextLine().trim();
            if (nl.length() < 3 || !nl.substring(0, 2).equals("X("))
                continue;
            makeConstId(nl.substring(2, nl.length() - 1));
            ++known_id_count;
        }

        // Seems like we need to use a Design to create SiteInsts to probe alternate site types...
        Design des = new Design("top",  args[0]);
        Device dev = des.getDevice();
        // Init valid cell placements
        init_bel_to_celltypes(des);

        ArrayList<NextpnrPackage> packages = new ArrayList();
        for (String pkg_name : dev.getPackages())
            packages.add(new NextpnrPackage(dev, dev.getPackage(pkg_name)));

        // Init tile type content
        Collection<Tile> tiles = dev.getAllTiles();
        ArrayList<NextpnrTileType> tile_types = new ArrayList();

        for (Tile t : tiles) {
            TileTypeEnum tte = t.getTileTypeEnum();
            if (flat_tiles.containsKey(tte))
                continue;
            DedupTileFlatWires fw = new DedupTileFlatWires(t);
            int type_index = flat_tiles.size();
            tile_type_idx.put(tte, type_index);
            flat_tiles.put(tte, fw);

            NextpnrTileType ntt = new NextpnrTileType(type_index, tte);
            ntt.do_import(des, t);
            tile_types.add(ntt);
        }

        ArrayList<NextpnrTileInst> tile_insts = new ArrayList();
        ArrayList<DedupTileInst> tile_shapes = new ArrayList();
        HashMap<String, Integer> tile_shape_idx = new HashMap();
        for (int row = 0; row < dev.getRows(); row++) {
            // Process columns backwards so we've collected all constants at the point of generating the row-const node
            row_gnd.clear();
            row_vcc.clear();
            ArrayList<NextpnrTileInst> row_tile_insts = new ArrayList();
            for (int col = dev.getColumns() - 1; col >= 0; col--) {
                Tile t = dev.getTile(row, col);
                DedupTileInst dti = new DedupTileInst(dev, t);
                String hash = dti.hash();
                int shape_idx;
                if (tile_shape_idx.containsKey(hash)) {
                    shape_idx = tile_shape_idx.get(hash);
                } else {
                    shape_idx = tile_shapes.size();
                    tile_shape_idx.put(hash, tile_shapes.size());
                    tile_shapes.add(dti);
                }
                row_tile_insts.add(new NextpnrTileInst(t, shape_idx));
            }
            // Permute order back
            for (int col = dev.getColumns() - 1; col >= 0; col--) {
                tile_insts.add(row_tile_insts.get(col));
            }
            System.err.printf("processed row %d/%d\n", (row+1), dev.getRows());
        }


        FileWriter bbaf = new FileWriter(args[2], false);
        PrintWriter bba = new PrintWriter(bbaf);


        // Header
        bba.println("pre #include \"nextpnr.h\"");
        bba.println("pre NEXTPNR_NAMESPACE_BEGIN");
        bba.println("post NEXTPNR_NAMESPACE_END");
        bba.println("push chipdb_blob");
        bba.println("ref chip_info chip_info");

        for (NextpnrTileType ntt : tile_types)
            ntt.write_content_bba(bba);
        bba.println("label tile_types");
        for (NextpnrTileType ntt : tile_types)
            ntt.write_bba(bba);

        for (NextpnrTileInst nti : tile_insts)
            nti.write_content_bba(bba);
        bba.println("label tile_insts");
        for (NextpnrTileInst nti : tile_insts)
            nti.write_bba(bba);

        for (int i = 0; i < node_shapes.size(); i++)
            node_shapes.get(i).write_content_bba(i, bba);
        bba.println("label node_shapes");
        for (int i = 0; i < node_shapes.size(); i++)
            node_shapes.get(i).write_bba(i, bba);

        for (int i = 0; i < tile_shapes.size(); i++)
            tile_shapes.get(i).write_content_bba(i, bba);
        bba.println("label tile_shapes");
        for (int i = 0; i < tile_shapes.size(); i++)
            tile_shapes.get(i).write_bba(i, bba);

        ArrayList<NextpnrMacro> macros = new ArrayList();
        for (EDIFCell macro : Design.getMacroPrimitives(dev.getSeries()).getCells())
            macros.add(new NextpnrMacro(macro));

        for (NextpnrMacro nm : macros)
            nm.write_content_bba(bba);
        bba.println("label macros");
        for (NextpnrMacro nm : macros)
            nm.write_bba(bba);

        ArrayList<NextpnrCellType> cell_types = get_cell_types(des);

        for (NextpnrCellType ct : cell_types)
            ct.write_content_bba(bba);
        bba.println("label cell_types");
        for (NextpnrCellType ct : cell_types)
            ct.write_bba(bba);

        for (int i = 0; i < pin_maps.size(); i++)
            pin_maps.get(i).write_content_bba(i, bba);
        bba.println("label pin_maps");
        for (int i = 0; i < pin_maps.size(); i++)
            pin_maps.get(i).write_bba(i, bba);

        for (NextpnrPackage np : packages)
            np.write_content_bba(bba);
        bba.println("label packages");
        for (NextpnrPackage np : packages)
            np.write_bba(bba);

        int name_id = makeConstId(dev.getName());

        bba.println("label extra_constid_strs");
        for (int i = known_id_count; i < constIds.size(); i++)
            bba.printf("str |%s|\n", constIds.get(i));

        bba.println("label extra_constids");
        bba.printf("u32 %d\n", known_id_count);
        bba.println("ref extra_constid_strs");
        bba.printf("u32 %d\n", constIds.size() - known_id_count);

        bba.println("label chip_info");
        bba.printf("u32 %d\n", name_id); // device name
        bba.printf("u32 %d\n", chipdb_version);
        bba.printf("u32 %d\n", dev.getColumns());
        bba.printf("u32 %d\n", dev.getRows());
        bba.printf("ref tile_types\n");
        bba.printf("u32 %d\n", tile_types.size());
        bba.printf("ref tile_insts\n");
        bba.printf("u32 %d\n", tile_insts.size());
        bba.printf("ref node_shapes\n");
        bba.printf("u32 %d\n", node_shapes.size());
        bba.printf("ref tile_shapes\n");
        bba.printf("u32 %d\n", tile_shapes.size());
        bba.printf("ref pin_maps\n");
        bba.printf("u32 %d\n", pin_maps.size());
        bba.printf("ref cell_types\n");
        bba.printf("u32 %d\n", cell_types.size());
        bba.printf("ref macros\n");
        bba.printf("u32 %d\n", macros.size());
        bba.printf("ref packages\n");
        bba.printf("u32 %d\n", packages.size());
        bba.printf("ref extra_constids\n");
        bba.println("pop");
        bbaf.close();
    }
}
