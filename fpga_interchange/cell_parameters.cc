/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#include "cell_parameters.h"

#include <limits>

#include "DeviceResources.capnp.h"
#include "context.h"
#include "log.h"
#include "nextpnr_assertions.h"

NEXTPNR_NAMESPACE_BEGIN

CellParameters::CellParameters()
        // 1'b0
        : verilog_binary_re("([1-9][0-9]*)'b([01]+)$"),
          // 8'hF
          verilog_hex_re("([1-9][0-9]*)'h([0-9a-fA-F]+)$"),
          // 0b10
          c_binary_re("0b([01]+)$"),
          // 0xF
          c_hex_re("0x([0-9a-fA-F]+)$")
{
}

void CellParameters::init(const Context *ctx)
{
    for (const CellParameterPOD &cell_parameter : ctx->chip_info->cell_map->cell_parameters) {
        IdString cell_type(cell_parameter.cell_type);
        IdString parameter(cell_parameter.parameter);
        auto result = parameters.emplace(std::make_pair(cell_type, parameter), &cell_parameter);
        NPNR_ASSERT(result.second);
    }
}

static bool parse_int(const std::string &data, int64_t *result)
{
    NPNR_ASSERT(result != nullptr);
    try {
        *result = boost::lexical_cast<int64_t>(data);
        return true;
    } catch (boost::bad_lexical_cast &e) {
        return false;
    }
}

DynamicBitarray<> CellParameters::parse_int_like(const Context *ctx, IdString cell_type, IdString parameter,
                                                 const Property &property) const
{
    const CellParameterPOD *definition = parameters.at(std::make_pair(cell_type, parameter));
    DeviceResources::Device::ParameterFormat format;
    format = static_cast<DeviceResources::Device::ParameterFormat>(definition->format);

    DynamicBitarray<> result;
    switch (format) {
    case DeviceResources::Device::ParameterFormat::BOOLEAN:
        result.resize(1);
        if (property.is_string) {
            if (property.as_string() == "TRUE" || property.as_string() == "1") {
                result.set(0, true);
            } else if (property.as_string() == "FALSE" || property.as_string() == "0") {
                result.set(0, false);
            } else {
                log_error("Property value %s not expected for BOOLEAN type.\n", property.c_str());
            }
        } else {
            if (property.intval == 1) {
                result.set(0, true);
            } else if (property.intval == 0) {
                result.set(0, false);
            } else {
                log_error("Property value %lu not expected for BOOLEAN type.\n", property.intval);
            }
        }

        return result;
    case DeviceResources::Device::ParameterFormat::INTEGER:
        if (property.is_string) {
            char *endptr;
            std::uintmax_t value = strtoumax(property.c_str(), &endptr, /*base=*/10);
            if (endptr != (property.c_str() + property.size())) {
                log_error("Property value %s not expected for INTEGER type.\n", property.c_str());
            }

            return DynamicBitarray<>::to_bitarray(value);
        } else {
            return DynamicBitarray<>::to_bitarray(property.intval);
        }
        break;
    case DeviceResources::Device::ParameterFormat::VERILOG_BINARY:
        if (property.is_string) {
            std::smatch m;
            if (!std::regex_match(property.as_string(), m, verilog_binary_re)) {
                log_error("Property value %s not expected for VERILOG_BINARY type.\n", property.c_str());
            }

            int64_t width;
            if (!parse_int(m[1], &width)) {
                log_error("Failed to parse width from property value %s of type VERILOG_BINARY.\n", property.c_str());
            }
            if (width < 0) {
                log_error("Expected width to be positive for property value %s\n", property.c_str());
            }

            return DynamicBitarray<>::parse_binary_bitstring(width, m[2]);
        } else {
            return DynamicBitarray<>::to_bitarray(property.intval);
        }
        break;
    case DeviceResources::Device::ParameterFormat::VERILOG_HEX:
        if (property.is_string) {
            std::smatch m;
            if (!std::regex_match(property.as_string(), m, verilog_hex_re)) {
                log_error("Property value %s not expected for VERILOG_HEX type.\n", property.c_str());
            }

            int64_t width;
            if (!parse_int(m[1], &width)) {
                log_error("Failed to parse width from property value %s of type VERILOG_HEX.\n", property.c_str());
            }
            if (width < 0) {
                log_error("Expected width to be positive for property value %s\n", property.c_str());
            }

            return DynamicBitarray<>::parse_hex_bitstring(width, m[2]);
        } else {
            return DynamicBitarray<>::to_bitarray(property.intval);
        }
        break;
    case DeviceResources::Device::ParameterFormat::C_BINARY:
        if (property.is_string) {
            std::smatch m;
            if (!std::regex_match(property.as_string(), m, c_binary_re)) {
                log_error("Property value %s not expected for C_BINARY type.\n", property.c_str());
            }

            return DynamicBitarray<>::parse_binary_bitstring(/*width=*/-1, m[1]);
        } else {
            return DynamicBitarray<>::to_bitarray(property.intval);
        }
        break;
    case DeviceResources::Device::ParameterFormat::C_HEX:
        if (property.is_string) {
            std::smatch m;
            if (!std::regex_match(property.as_string(), m, c_hex_re)) {
                log_error("Property value %s not expected for C_HEX type.\n", property.c_str());
            }

            return DynamicBitarray<>::parse_hex_bitstring(/*width=*/-1, m[1]);
        } else {
            return DynamicBitarray<>::to_bitarray(property.intval);
        }
        break;
    default:
        log_error("Format %d is not int-like\n", definition->format);
    }

    // Unreachable!
    NPNR_ASSERT(false);
}

bool CellParameters::compare_property(const Context *ctx, IdString cell_type, IdString parameter,
                                      const Property &property, IdString value_to_compare) const
{
    const CellParameterPOD *definition = parameters.at(std::make_pair(cell_type, parameter));
    DeviceResources::Device::ParameterFormat format;
    format = static_cast<DeviceResources::Device::ParameterFormat>(definition->format);

    switch (format) {
    case DeviceResources::Device::ParameterFormat::STRING:
        return value_to_compare.c_str(ctx) == property.as_string();
    case DeviceResources::Device::ParameterFormat::FLOATING_POINT:
        // Note: Comparing floating point is pretty weird
        log_warning("Doing direct comparisions on floating points values is pretty weird, double check this.  Cell "
                    "type %s parameter %s\n",
                    cell_type.c_str(ctx), parameter.c_str(ctx));
        return value_to_compare.c_str(ctx) == property.as_string();
    case DeviceResources::Device::ParameterFormat::BOOLEAN:
    case DeviceResources::Device::ParameterFormat::INTEGER:
    case DeviceResources::Device::ParameterFormat::VERILOG_BINARY:
    case DeviceResources::Device::ParameterFormat::VERILOG_HEX:
    case DeviceResources::Device::ParameterFormat::C_BINARY:
    case DeviceResources::Device::ParameterFormat::C_HEX: {
        if (property.is_string) {
            // Given that string presentations should be equivalent if
            // formatted consistently, this should work most and or all of
            // the time.  If there are important exceptions, revisit this.
            return property.as_string() == value_to_compare.c_str(ctx);
        } else {
            int64_t int_to_compare;
            if (!parse_int(value_to_compare.c_str(ctx), &int_to_compare)) {
                log_error("Comparision failed, to compare value %s is not int-like\n", value_to_compare.c_str(ctx));
            }

            return property.intval == int_to_compare;
        }
    }
    }

    // Unreachable!
    NPNR_ASSERT(false);
}

NEXTPNR_NAMESPACE_END
