// Common Python bindings #included by all arches

readonly_wrapper<Context, decltype(&Context::cells), &Context::cells, wrap_context<CellMap &>>::def_wrap(ctx_cls,
                                                                                                         "cells");
readonly_wrapper<Context, decltype(&Context::nets), &Context::nets, wrap_context<NetMap &>>::def_wrap(ctx_cls, "nets");
readonly_wrapper<Context, decltype(&Context::net_aliases), &Context::net_aliases, wrap_context<AliasMap &>>::def_wrap(
        ctx_cls, "net_aliases");
readonly_wrapper<Context, decltype(&Context::hierarchy), &Context::hierarchy, wrap_context<HierarchyMap &>>::def_wrap(
        ctx_cls, "hierarchy");
readwrite_wrapper<Context, decltype(&Context::top_module), &Context::top_module, conv_to_str<IdString>,
                  conv_from_str<IdString>>::def_wrap(ctx_cls, "top_module");
readonly_wrapper<Context, decltype(&Context::timing_result), &Context::timing_result,
                 wrap_context<TimingResult &>>::def_wrap(ctx_cls, "timing_result");

fn_wrapper_0a<Context, decltype(&Context::getNameDelimiter), &Context::getNameDelimiter, pass_through<char>>::def_wrap(
        ctx_cls, "getNameDelimiter");

fn_wrapper_1a<Context, decltype(&Context::getNetByAlias), &Context::getNetByAlias, deref_and_wrap<NetInfo>,
              conv_from_str<IdString>>::def_wrap(ctx_cls, "getNetByAlias");
fn_wrapper_2a_v<Context, decltype(&Context::addClock), &Context::addClock, conv_from_str<IdString>,
                pass_through<float>>::def_wrap(ctx_cls, "addClock");
fn_wrapper_5a_v<Context, decltype(&Context::createRectangularRegion), &Context::createRectangularRegion,
                conv_from_str<IdString>, pass_through<int>, pass_through<int>, pass_through<int>,
                pass_through<int>>::def_wrap(ctx_cls, "createRectangularRegion");
fn_wrapper_2a_v<Context, decltype(&Context::addBelToRegion), &Context::addBelToRegion, conv_from_str<IdString>,
                conv_from_str<BelId>>::def_wrap(ctx_cls, "addBelToRegion");
fn_wrapper_2a_v<Context, decltype(&Context::constrainCellToRegion), &Context::constrainCellToRegion,
                conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "constrainCellToRegion");

fn_wrapper_2a<Context, decltype(&Context::getNetinfoRouteDelay), &Context::getNetinfoRouteDelay, pass_through<delay_t>,
              addr_and_unwrap<NetInfo>, unwrap_context<PortRef &>>::def_wrap(ctx_cls, "getNetinfoRouteDelay");

fn_wrapper_1a<Context, decltype(&Context::createNet), &Context::createNet, deref_and_wrap<NetInfo>,
              conv_from_str<IdString>>::def_wrap(ctx_cls, "createNet");
fn_wrapper_3a_v<Context, decltype(&Context::connectPort), &Context::connectPort, conv_from_str<IdString>,
                conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "connectPort");
fn_wrapper_2a_v<Context, decltype(&Context::disconnectPort), &Context::disconnectPort, conv_from_str<IdString>,
                conv_from_str<IdString>>::def_wrap(ctx_cls, "disconnectPort");
fn_wrapper_1a_v<Context, decltype(&Context::ripupNet), &Context::ripupNet, conv_from_str<IdString>>::def_wrap(
        ctx_cls, "ripupNet");
fn_wrapper_1a_v<Context, decltype(&Context::lockNetRouting), &Context::lockNetRouting,
                conv_from_str<IdString>>::def_wrap(ctx_cls, "lockNetRouting");

fn_wrapper_2a<Context, decltype(&Context::createCell), &Context::createCell, deref_and_wrap<CellInfo>,
              conv_from_str<IdString>, conv_from_str<IdString>>::def_wrap(ctx_cls, "createCell");
fn_wrapper_2a_v<Context, decltype(&Context::copyBelPorts), &Context::copyBelPorts, conv_from_str<IdString>,
                conv_from_str<BelId>>::def_wrap(ctx_cls, "copyBelPorts");

fn_wrapper_1a<Context, decltype(&Context::getBelType), &Context::getBelType, conv_to_str<IdString>,
              conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelType");
fn_wrapper_1a<Context, decltype(&Context::getBelLocation), &Context::getBelLocation, pass_through<Loc>,
              conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelLocation");
fn_wrapper_1a<Context, decltype(&Context::checkBelAvail), &Context::checkBelAvail, pass_through<bool>,
              conv_from_str<BelId>>::def_wrap(ctx_cls, "checkBelAvail");
fn_wrapper_1a<Context, decltype(&Context::getBelChecksum), &Context::getBelChecksum, pass_through<uint32_t>,
              conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelChecksum");
fn_wrapper_3a_v<Context, decltype(&Context::bindBel), &Context::bindBel, conv_from_str<BelId>,
                addr_and_unwrap<CellInfo>, pass_through<PlaceStrength>>::def_wrap(ctx_cls, "bindBel");
fn_wrapper_1a_v<Context, decltype(&Context::unbindBel), &Context::unbindBel, conv_from_str<BelId>>::def_wrap(
        ctx_cls, "unbindBel");
fn_wrapper_1a<Context, decltype(&Context::getBoundBelCell), &Context::getBoundBelCell, deref_and_wrap<CellInfo>,
              conv_from_str<BelId>>::def_wrap(ctx_cls, "getBoundBelCell");
fn_wrapper_1a<Context, decltype(&Context::getConflictingBelCell), &Context::getConflictingBelCell,
              deref_and_wrap<CellInfo>, conv_from_str<BelId>>::def_wrap(ctx_cls, "getConflictingBelCell");
fn_wrapper_0a<Context, decltype(&Context::getBels), &Context::getBels, wrap_context<BelRange>>::def_wrap(ctx_cls,
                                                                                                         "getBels");

fn_wrapper_2a<Context, decltype(&Context::getBelPinWire), &Context::getBelPinWire, conv_to_str<WireId>,
              conv_from_str<BelId>, conv_from_str<IdString>>::def_wrap(ctx_cls, "getBelPinWire");
fn_wrapper_2a<Context, decltype(&Context::getBelPinType), &Context::getBelPinType, pass_through<PortType>,
              conv_from_str<BelId>, conv_from_str<IdString>>::def_wrap(ctx_cls, "getBelPinType");
fn_wrapper_1a<Context, decltype(&Context::getWireBelPins), &Context::getWireBelPins, wrap_context<BelPinRange>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "getWireBelPins");

fn_wrapper_1a<Context, decltype(&Context::getWireChecksum), &Context::getWireChecksum, pass_through<uint32_t>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "getWireChecksum");
fn_wrapper_1a<Context, decltype(&Context::getWireType), &Context::getWireType, conv_to_str<IdString>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "getWireType");
fn_wrapper_3a_v<Context, decltype(&Context::bindWire), &Context::bindWire, conv_from_str<WireId>,
                addr_and_unwrap<NetInfo>, pass_through<PlaceStrength>>::def_wrap(ctx_cls, "bindWire");
fn_wrapper_1a_v<Context, decltype(&Context::unbindWire), &Context::unbindWire, conv_from_str<WireId>>::def_wrap(
        ctx_cls, "unbindWire");
fn_wrapper_1a<Context, decltype(&Context::checkWireAvail), &Context::checkWireAvail, pass_through<bool>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "checkWireAvail");
fn_wrapper_1a<Context, decltype(&Context::getBoundWireNet), &Context::getBoundWireNet, deref_and_wrap<NetInfo>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "getBoundWireNet");
fn_wrapper_1a<Context, decltype(&Context::getConflictingWireNet), &Context::getConflictingWireNet,
              deref_and_wrap<NetInfo>, conv_from_str<WireId>>::def_wrap(ctx_cls, "getConflictingWireNet");

fn_wrapper_0a<Context, decltype(&Context::getWires), &Context::getWires, wrap_context<WireRange>>::def_wrap(ctx_cls,
                                                                                                            "getWires");

fn_wrapper_0a<Context, decltype(&Context::getPips), &Context::getPips, wrap_context<AllPipRange>>::def_wrap(ctx_cls,
                                                                                                            "getPips");
fn_wrapper_1a<Context, decltype(&Context::getPipChecksum), &Context::getPipChecksum, pass_through<uint32_t>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipChecksum");
fn_wrapper_1a<Context, decltype(&Context::getPipLocation), &Context::getPipLocation, pass_through<Loc>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipLocation");
fn_wrapper_3a_v<Context, decltype(&Context::bindPip), &Context::bindPip, conv_from_str<PipId>, addr_and_unwrap<NetInfo>,
                pass_through<PlaceStrength>>::def_wrap(ctx_cls, "bindPip");
fn_wrapper_1a_v<Context, decltype(&Context::unbindPip), &Context::unbindPip, conv_from_str<PipId>>::def_wrap(
        ctx_cls, "unbindPip");
fn_wrapper_1a<Context, decltype(&Context::checkPipAvail), &Context::checkPipAvail, pass_through<bool>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "checkPipAvail");
fn_wrapper_1a<Context, decltype(&Context::getBoundPipNet), &Context::getBoundPipNet, deref_and_wrap<NetInfo>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "getBoundPipNet");
fn_wrapper_1a<Context, decltype(&Context::getConflictingPipNet), &Context::getConflictingPipNet,
              deref_and_wrap<NetInfo>, conv_from_str<PipId>>::def_wrap(ctx_cls, "getConflictingPipNet");

fn_wrapper_1a<Context, decltype(&Context::getPipsDownhill), &Context::getPipsDownhill, wrap_context<DownhillPipRange>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "getPipsDownhill");
fn_wrapper_1a<Context, decltype(&Context::getPipsUphill), &Context::getPipsUphill, wrap_context<UphillPipRange>,
              conv_from_str<WireId>>::def_wrap(ctx_cls, "getPipsUphill");

fn_wrapper_1a<Context, decltype(&Context::getPipSrcWire), &Context::getPipSrcWire, conv_to_str<WireId>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipSrcWire");
fn_wrapper_1a<Context, decltype(&Context::getPipDstWire), &Context::getPipDstWire, conv_to_str<WireId>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDstWire");
fn_wrapper_1a<Context, decltype(&Context::getPipDelay), &Context::getPipDelay, pass_through<DelayQuad>,
              conv_from_str<PipId>>::def_wrap(ctx_cls, "getPipDelay");

fn_wrapper_0a<Context, decltype(&Context::getChipName), &Context::getChipName, pass_through<std::string>>::def_wrap(
        ctx_cls, "getChipName");
fn_wrapper_0a<Context, decltype(&Context::archId), &Context::archId, conv_to_str<IdString>>::def_wrap(ctx_cls,
                                                                                                      "archId");

fn_wrapper_2a_v<Context, decltype(&Context::writeSVG), &Context::writeSVG, pass_through<std::string>,
                pass_through<std::string>>::def_wrap(ctx_cls, "writeSVG");

fn_wrapper_2a<Context, decltype(&Context::isBelLocationValid), &Context::isBelLocationValid, pass_through<bool>,
              conv_from_str<BelId>, pass_through<bool>>::def_wrap(ctx_cls, "isBelLocationValid");

// const\_range\<BelBucketId\> getBelBuckets() const
fn_wrapper_0a<Context, decltype(&Context::getBelBuckets), &Context::getBelBuckets,
              wrap_context<BelBucketRange>>::def_wrap(ctx_cls, "getBelBuckets");
// BelBucketId getBelBucketForBel(BelId bel) const
fn_wrapper_1a<Context, decltype(&Context::getBelBucketForBel), &Context::getBelBucketForBel, conv_to_str<BelBucketId>,
              conv_from_str<BelId>>::def_wrap(ctx_cls, "getBelBucketForBel");
// BelBucketId getBelBucketForCellType(IdString cell\_type) const
fn_wrapper_1a<Context, decltype(&Context::getBelBucketForCellType), &Context::getBelBucketForCellType,
              conv_to_str<BelBucketId>, conv_from_str<IdString>>::def_wrap(ctx_cls, "getBelBucketForCellType");
// const\_range\<BelId\> getBelsInBucket(BelBucketId bucket) const
fn_wrapper_1a<Context, decltype(&Context::getBelsInBucket), &Context::getBelsInBucket,
              wrap_context<BelRangeForBelBucket>, conv_from_str<BelBucketId>>::def_wrap(ctx_cls, "getBelsInBucket");
// bool isValidBelForCellType(IdString cell\_type, BelId bel) const
fn_wrapper_2a<Context, decltype(&Context::isValidBelForCellType), &Context::isValidBelForCellType, pass_through<bool>,
              conv_from_str<IdString>, conv_from_str<BelId>>::def_wrap(ctx_cls, "isValidBelForCellType");

fn_wrapper_1a<Context, decltype(&Context::getDelayFromNS), &Context::getDelayFromNS, pass_through<delay_t>,
              pass_through<double>>::def_wrap(ctx_cls, "getDelayFromNS");

fn_wrapper_1a<Context, decltype(&Context::getDelayNS), &Context::getDelayNS, pass_through<double>,
              pass_through<delay_t>>::def_wrap(ctx_cls, "getDelayNS");

fn_wrapper_3a_v<Context, decltype(&Context::createRegionPlug), &Context::createRegionPlug, conv_from_str<IdString>,
                conv_from_str<IdString>, pass_through<Loc>>::def_wrap(ctx_cls, "createRegionPlug");
fn_wrapper_4a_v<Context, decltype(&Context::addPlugPin), &Context::addPlugPin, conv_from_str<IdString>,
                conv_from_str<IdString>, pass_through<PortType>, conv_from_str<WireId>>::def_wrap(ctx_cls,
                                                                                                  "addPlugPin");
