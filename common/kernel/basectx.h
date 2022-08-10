/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

#ifndef BASECTX_H
#define BASECTX_H

#include <mutex>
#include <unordered_map>
#include <vector>
#ifndef NPNR_DISABLE_THREADS
#include <boost/thread.hpp>
#endif

#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "property.h"
#include "str_ring_buffer.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

struct BaseCtx
{
#ifndef NPNR_DISABLE_THREADS
    // Lock to perform mutating actions on the Context.
    std::mutex mutex;
    boost::thread::id mutex_owner;

    // Lock to be taken by UI when wanting to access context - the yield()
    // method will lock/unlock it when its' released the main mutex to make
    // sure the UI is not starved.
    std::mutex ui_mutex;
#endif

    // ID String database.
    mutable std::unordered_map<std::string, int> *idstring_str_to_idx;
    mutable std::vector<const std::string *> *idstring_idx_to_str;

    // Temporary string backing store for logging
    mutable StrRingBuffer log_strs;

    // Project settings and config switches
    dict<IdString, Property> settings;

    // Placed nets and cells.
    dict<IdString, std::unique_ptr<NetInfo>> nets;
    dict<IdString, std::unique_ptr<CellInfo>> cells;

    // Hierarchical (non-leaf) cells by full path
    dict<IdString, HierarchicalCell> hierarchy;
    // This is the root of the above structure
    IdString top_module;

    // Aliases for nets, which may have more than one name due to assignments and hierarchy
    dict<IdString, IdString> net_aliases;

    // Top-level ports
    dict<IdString, PortInfo> ports;
    dict<IdString, CellInfo *> port_cells;

    // Floorplanning regions
    dict<IdString, std::unique_ptr<Region>> region;

    // Context meta data
    dict<IdString, Property> attrs;

    // Fmax data post timing analysis
    TimingResult timing_result;

    Context *as_ctx = nullptr;

    // Has the frontend loaded a design?
    bool design_loaded;

    BaseCtx()
    {
        idstring_str_to_idx = new std::unordered_map<std::string, int>;
        idstring_idx_to_str = new std::vector<const std::string *>;
        IdString::initialize_add(this, "", 0);
        IdString::initialize_arch(this);

        design_loaded = false;
    }

    virtual ~BaseCtx()
    {
        delete idstring_str_to_idx;
        delete idstring_idx_to_str;
    }

    // Must be called before performing any mutating changes on the Ctx/Arch.
    void lock(void)
    {
#ifndef NPNR_DISABLE_THREADS
        mutex.lock();
        mutex_owner = boost::this_thread::get_id();
#endif
    }

    void unlock(void)
    {
#ifndef NPNR_DISABLE_THREADS
        NPNR_ASSERT(boost::this_thread::get_id() == mutex_owner);
        mutex.unlock();
#endif
    }

    // Must be called by the UI before rendering data. This lock will be
    // prioritized when processing code calls yield().
    void lock_ui(void)
    {
#ifndef NPNR_DISABLE_THREADS
        ui_mutex.lock();
        mutex.lock();
#endif
    }

    void unlock_ui(void)
    {
#ifndef NPNR_DISABLE_THREADS
        mutex.unlock();
        ui_mutex.unlock();
#endif
    }

    // Yield to UI by unlocking the main mutex, flashing the UI mutex and
    // relocking the main mutex. Call this when you're performing a
    // long-standing action while holding a lock to let the UI show
    // visualization updates.
    // Must be called with the main lock taken.
    void yield(void)
    {
#ifndef NPNR_DISABLE_THREADS
        unlock();
        ui_mutex.lock();
        ui_mutex.unlock();
        lock();
#endif
    }

    IdString id(const std::string &s) const { return IdString(this, s); }

    IdString id(const char *s) const { return IdString(this, s); }

    IdString idf(const char *fmt, ...) const; // create IdString using printf formatting

    Context *getCtx() { return as_ctx; }

    const Context *getCtx() const { return as_ctx; }

    const char *nameOf(IdString name) const { return name.c_str(this); }

    template <typename T> const char *nameOf(const T *obj) const
    {
        if (obj == nullptr)
            return "";
        return obj->name.c_str(this);
    }

    const char *nameOfBel(BelId bel) const;
    const char *nameOfWire(WireId wire) const;
    const char *nameOfPip(PipId pip) const;
    const char *nameOfGroup(GroupId group) const;

    // Wrappers of arch functions that take a string and handle IdStringList parsing
    BelId getBelByNameStr(const std::string &str);
    WireId getWireByNameStr(const std::string &str);
    PipId getPipByNameStr(const std::string &str);
    GroupId getGroupByNameStr(const std::string &str);

    // --------------------------------------------------------------

    bool allUiReload = true;
    bool frameUiReload = false;
    pool<BelId> belUiReload;
    pool<WireId> wireUiReload;
    pool<PipId> pipUiReload;
    pool<GroupId> groupUiReload;

    void refreshUi() { allUiReload = true; }

    void refreshUiFrame() { frameUiReload = true; }

    void refreshUiBel(BelId bel) { belUiReload.insert(bel); }

    void refreshUiWire(WireId wire) { wireUiReload.insert(wire); }

    void refreshUiPip(PipId pip) { pipUiReload.insert(pip); }

    void refreshUiGroup(GroupId group) { groupUiReload.insert(group); }

    // --------------------------------------------------------------

    NetInfo *getNetByAlias(IdString alias) const
    {
        return nets.count(alias) ? nets.at(alias).get() : nets.at(net_aliases.at(alias)).get();
    }

    // Intended to simplify Python API
    void addClock(IdString net, float freq);
    void createRectangularRegion(IdString name, int x0, int y0, int x1, int y1);
    void addBelToRegion(IdString name, BelId bel);
    void constrainCellToRegion(IdString cell, IdString region_name);

    // Helper functions for the partial reconfiguration plug API using PseudoCells
    void createRegionPlug(IdString name, IdString type, Loc approx_loc);
    void addPlugPin(IdString plug, IdString pin, PortType dir, WireId wire);

    // Helper functions for Python bindings
    NetInfo *createNet(IdString name);
    void connectPort(IdString net, IdString cell, IdString port);
    void disconnectPort(IdString cell, IdString port);
    void ripupNet(IdString name);
    void lockNetRouting(IdString name);
    void renameNet(IdString old_name, IdString new_name);

    CellInfo *createCell(IdString name, IdString type);
    void copyBelPorts(IdString cell, BelId bel);

    // Workaround for lack of wrappable constructors
    DecalXY constructDecalXY(DecalId decal, float x, float y);

    void archInfoToAttributes();
    void attributesToArchInfo();
};

NEXTPNR_NAMESPACE_END

#endif /* BASECTX_H */
