/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  NG-Ultra Architecture Implementation
 *
 *  Copyright (C) 2024  YosysHQ GmbH
 *
 */

#include <vector>
#include "log.h"
#include "gtest/gtest.h"

USING_NEXTPNR_NAMESPACE

int main(int argc, char **argv)
{
    // TODO: Remove for delivery, useful while checking tests
    log_streams.push_back(std::make_pair(&std::cerr, LogLevel::LOG_MSG));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
