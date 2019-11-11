/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <dave@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * Generic Frontend Framework
 *
 * This is designed to make it possible to build frontends for parsing any format isomorphic to Yosys JSON [1]
 * with maximal inlining and minimal need for overhead such as runtime polymorphism or extra wrapper types.
 *
 * [1] http://www.clifford.at/yosys/cmd_write_json.html
 *
 * The frontend should implement a class referred to as FrontendType that defines the following type(def)s and
 * functions:
 *
 * Types:
 *   ModuleDataType: corresponds to a single entry in "modules"
 *   ModulePortDataType: corresponds to a single entry in "ports" of a module
 *   CellDataType: corresponds to a single entry in "cells"
 *   NetnameDataType: corresponds to a single entry in "netnames"
 *   BitVectorDataType: corresponds to a signal/constant bit vector (e.g. a "connections" field)
 *
 * Functions:
 *
 *   void foreach_module(Func);
 *       calls Func(const std::string &name, const ModuleDataType &mod);
 *       for each module in the netlist
 *
 *   void foreach_port(const ModuleDataType &mod, Func);
 *       calls Func(const std::string &name, const ModulePortDataType &port);
 *       for each port of mod
 *
 *   void foreach_cell(const ModuleDataType &mod, Func);
 *       calls Func(const std::string &name, const CellDataType &cell);
 *       for each cell of mod
 *
 *   void foreach_netname(const ModuleDataType &mod, Func);
 *       calls Func(const std::string &name, const NetnameDataType &cell);
 *       for each netname entry of mod
 *
 *   PortType get_port_dir(const ModulePortDataType &port);
 *       gets the PortType direction of a module port
 *
 *   int get_port_offset(const ModulePortDataType &port);
 *       gets the start bit number of a port
 *
 *   bool is_port_upto(const ModulePortDataType &port);
 *       returns true if a port is an "upto" type port
 *
 *   const BitVectorDataType &get_port_bits(const ModulePortDataType &port);
 *       gets the bit vector of a module port
 *
 *   const std::string& get_cell_type(const CellDataType &cell);
 *       gets the type of a cell
 *
 *   void foreach_attr(const {ModuleDataType|CellDataType|ModulePortDataType|NetnameDataType} &obj, Func);
 *       calls Func(const std::string &name, const Property &value);
 *       for each attribute on a module, cell, module port or net
 *
 *   void foreach_param(const CellDataType &obj, Func);
 *       calls Func(const std::string &name, const Property &value);
 *       for each parameter of a cell
 *
 *   void foreach_port_dir(const CellDataType &cell, Func);
 *       calls Func(const std::string &name, PortType dir);
 *       for each port direction of a cell
 *
 *   void foreach_port_conn(const CellDataType &cell, Func);
 *       calls Func(const std::string &name, const BitVectorDataType &conn);
 *       for each port connection of a cell
 *
 *   const BitVectorDataType &get_net_bits(const NetnameDataType &net);
 *       gets the BitVector corresponding to the bits entry of a netname field
 *
 *   int get_vector_length(const BitVectorDataType &bits);
 *       gets the length of a BitVector
 *
 *   bool is_vector_bit_constant(const BitVectorDataType &bits, int i);
 *       returns true if bit <i> of bits is constant
 *
 *   char get_vector_bit_constval(const BitVectorDataType &bits, int i);
 *       returns a char [01xz] corresponding to the constant value of bit <i>
 *
 *   int get_vector_bit_signal(const BitVectorDataType &bits, int i);
 *       returns the signal number of vector bit <i>
 *
 */