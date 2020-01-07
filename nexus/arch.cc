/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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

#include <boost/algorithm/string.hpp>

#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);

#include "constids.inc"

#undef X
}

// -----------------------------------------------------------------------

static const DatabasePOD *get_chipdb(const RelPtr<DatabasePOD> *ptr) { return ptr->get(); }

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    // Parse device string
    if (boost::starts_with(args.device, "LIFCL")) {
        family = "LIFCL";
    } else {
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    }
    auto last_sep = args.device.rfind('-');
    if (last_sep == std::string::npos)
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    device = args.device.substr(0, last_sep);
    speed = args.device.substr(last_sep + 1, 1);
    auto package_end = args.device.find_last_of("0123456789");
    if (package_end == std::string::npos || package_end < last_sep)
        log_error("Unknown device string '%s' (expected device name like 'LIFCL-40-8SG72C')\n", args.device.c_str());
    package = args.device.substr(last_sep + 2, (package_end - (last_sep + 2)) + 1);
    rating = args.device.substr(package_end + 1);
    // Load database (FIXME: baked-in databases too)
    try {
        blob_file.open(args.chipdb);
        if (!blob_file.is_open())
            log_error("Unable to read chipdb %s\n", args.chipdb.c_str());
        const char *blob = reinterpret_cast<const char *>(blob_file.data());
        db = get_chipdb(reinterpret_cast<const RelPtr<DatabasePOD> *>(blob));
    } catch (...) {
        log_error("Unable to read chipdb %s\n", args.chipdb.c_str());
    }
    // Check database version and family
    if (db->version != bba_version) {
        log_error("Provided database version %d is %s than nextpnr version %d, please rebuild database/nextpnr.\n",
                  db->version, (db->version > bba_version) ? "newer" : "older", bba_version);
    }
    if (db->family.get() != family) {
        log_error("Database is for family '%s' but provided device is family '%s'.\n", db->family.get(),
                  family.c_str());
    }
    // Set up chip_info
    chip_info = nullptr;
    for (size_t i = 0; i < db->num_chips; i++) {
        auto &chip = db->chips[i];
        if (chip.device_name.get() == device) {
            chip_info = &chip;
            break;
        }
    }
    if (!chip_info)
        log_error("Unknown device '%s'.\n", device.c_str());
    // Set up bba IdStrings
    for (size_t i = 0; i < db->ids->num_bba_ids; i++) {
        IdString::initialize_add(this, db->ids->bba_id_strs[i].get(), uint32_t(i) + db->ids->num_file_ids);
    }
    // Set up validity structures
    tileStatus.resize(chip_info->num_tiles);
    for (size_t i = 0; i < chip_info->num_tiles; i++) {
        tileStatus[i].boundcells.resize(db->loctypes[chip_info->grid[i].loc_type].num_bels);
    }
}

NEXTPNR_NAMESPACE_END