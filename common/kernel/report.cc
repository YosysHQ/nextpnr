/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "json11.hpp"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

using namespace json11;

namespace {
dict<IdString, std::pair<int, int>> get_utilization(const Context *ctx)
{
    // Sort by Bel type
    dict<IdString, std::pair<int, int>> result;
    for (auto &cell : ctx->cells) {
        result[ctx->getBelBucketName(ctx->getBelBucketForCellType(cell.second.get()->type))].first++;
    }
    for (auto bel : ctx->getBels()) {
        if (!ctx->getBelHidden(bel)) {
            result[ctx->getBelBucketName(ctx->getBelBucketForBel(bel))].second++;
        }
    }
    return result;
}
} // namespace

static std::string clock_event_name(const Context *ctx, const ClockEvent &e)
{
    std::string value;
    if (e.clock == IdString() || e.clock == ctx->id("$async$"))
        value = std::string("<async>");
    else
        value = (e.edge == FALLING_EDGE ? std::string("negedge ") : std::string("posedge ")) + e.clock.str(ctx);
    return value;
};

static Json::array json_report_critical_paths(const Context *ctx)
{

    auto report_critical_path = [ctx](const CriticalPath &report) {
        Json::array pathJson;

        for (const auto &segment : report.segments) {

            const auto &driver = ctx->cells.at(segment.from.first);
            const auto &sink = ctx->cells.at(segment.to.first);

            auto fromLoc = ctx->getBelLocation(driver->bel);
            auto toLoc = ctx->getBelLocation(sink->bel);

            auto fromJson = Json::object({{"cell", segment.from.first.c_str(ctx)},
                                          {"port", segment.from.second.c_str(ctx)},
                                          {"loc", Json::array({fromLoc.x, fromLoc.y})}});

            auto toJson = Json::object({{"cell", segment.to.first.c_str(ctx)},
                                        {"port", segment.to.second.c_str(ctx)},
                                        {"loc", Json::array({toLoc.x, toLoc.y})}});

            auto segmentJson = Json::object({
                    {"delay", ctx->getDelayNS(segment.delay)},
                    {"from", fromJson},
                    {"to", toJson},
            });

            if (segment.type == CriticalPath::Segment::Type::CLK_TO_Q) {
                segmentJson["type"] = "clk-to-q";
            } else if (segment.type == CriticalPath::Segment::Type::SOURCE) {
                segmentJson["type"] = "source";
            } else if (segment.type == CriticalPath::Segment::Type::LOGIC) {
                segmentJson["type"] = "logic";
            } else if (segment.type == CriticalPath::Segment::Type::SETUP) {
                segmentJson["type"] = "setup";
            } else if (segment.type == CriticalPath::Segment::Type::ROUTING) {
                segmentJson["type"] = "routing";
                segmentJson["net"] = segment.net.c_str(ctx);
            }

            pathJson.push_back(segmentJson);
        }

        return pathJson;
    };

    auto critPathsJson = Json::array();

    // Critical paths
    for (auto &report : ctx->timing_result.clock_paths) {

        critPathsJson.push_back(Json::object({{"from", clock_event_name(ctx, report.second.clock_pair.start)},
                                              {"to", clock_event_name(ctx, report.second.clock_pair.end)},
                                              {"path", report_critical_path(report.second)}}));
    }

    // Cross-domain paths
    for (auto &report : ctx->timing_result.xclock_paths) {
        critPathsJson.push_back(Json::object({{"from", clock_event_name(ctx, report.clock_pair.start)},
                                              {"to", clock_event_name(ctx, report.clock_pair.end)},
                                              {"path", report_critical_path(report)}}));
    }

    return critPathsJson;
}

static Json::array json_report_detailed_net_timings(const Context *ctx)
{
    auto detailedNetTimingsJson = Json::array();

    // Detailed per-net timing analysis
    for (const auto &it : ctx->timing_result.detailed_net_timings) {

        const NetInfo *net = ctx->nets.at(it.first).get();
        ClockEvent start = it.second[0].clock_pair.start;

        Json::array endpointsJson;
        for (const auto &sink_timing : it.second) {
            auto endpointJson = Json::object({{"cell", sink_timing.cell_port.first.c_str(ctx)},
                                              {"port", sink_timing.cell_port.second.c_str(ctx)},
                                              {"event", clock_event_name(ctx, sink_timing.clock_pair.end)},
                                              {"delay", ctx->getDelayNS(sink_timing.delay)}});
            endpointsJson.push_back(endpointJson);
        }

        auto netTimingJson = Json::object({{"net", net->name.c_str(ctx)},
                                           {"driver", net->driver.cell->name.c_str(ctx)},
                                           {"port", net->driver.port.c_str(ctx)},
                                           {"event", clock_event_name(ctx, start)},
                                           {"endpoints", endpointsJson}});

        detailedNetTimingsJson.push_back(netTimingJson);
    }

    return detailedNetTimingsJson;
}

/*
Report JSON structure:

{
  "utilization": {
    <BEL name>: {
      "available": <available count>,
      "used": <used count>
    },
    ...
  },
  "fmax" {
    <clock name>: {
      "achieved": <achieved fmax [MHz]>,
      "constraint": <target fmax [MHz]>
    },
    ...
  },
  "critical_paths": [
    {
      "from": <clock event edge and name>,
      "to": <clock event edge and name>,
      "path": [
        {
          "from": {
            "cell": <driver cell name>
            "port": <driver port name>
            "loc": [
              <grid x>,
              <grid y>
            ]
          },
          "to": {
            "cell": <sink cell name>
            "port": <sink port name>
            "loc": [
              <grid x>,
              <grid y>
            ]
          },
          "type": <path segment type "clk-to-q", "source", "logic", "routing" or "setup">,
          "net": <net name (for routing only!)>,
          "delay": <segment delay [ns]>,
        }
        ...
      ]
    },
    ...
  ],
  "detailed_net_timings": [
    {
      "driver": <driving cell name>,
      "port": <driving cell port name>,
      "event": <driver clock event name>,
      "net": <net name>,
      "endpoints": [
        {
          "cell": <sink cell name>,
          "port": <sink cell port name>,
          "event": <destination clock event name>,
          "delay": <delay [ns]>,
        }
        ...
      ]
    }
    ...
  ]
}
*/

void Context::writeJsonReport(std::ostream &out) const
{
    auto util = get_utilization(this);
    dict<std::string, Json> util_json;
    for (const auto &kv : util) {
        util_json[kv.first.str(this)] = Json::object{
                {"used", kv.second.first},
                {"available", kv.second.second},
        };
    }
    dict<std::string, Json> fmax_json;
    for (const auto &kv : timing_result.clock_fmax) {
        fmax_json[kv.first.str(this)] = Json::object{
                {"achieved", kv.second.achieved},
                {"constraint", kv.second.constraint},
        };
    }

    Json::object jsonRoot{
            {"utilization", util_json}, {"fmax", fmax_json}, {"critical_paths", json_report_critical_paths(this)}};

    if (detailed_timing_report) {
        jsonRoot["detailed_net_timings"] = json_report_detailed_net_timings(this);
    }

    out << Json(jsonRoot).dump() << std::endl;
}

NEXTPNR_NAMESPACE_END
