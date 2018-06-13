#include <vector>
#include "gtest/gtest.h"
#include "nextpnr.h"

USING_NEXTPNR_NAMESPACE

class LP1KTest : public ::testing::Test
{
  protected:
    virtual void SetUp()
    {
        chipArgs.type = ChipArgs::LP1K;
        chipArgs.package = "tq144";
        design = new Design(chipArgs);
    }

    virtual void TearDown() { delete design; }

    ChipArgs chipArgs;
    Design *design;
};

TEST_F(LP1KTest, bel_names)
{
    int bel_count = 0;
    for (auto bel : design->chip.getBels()) {
        auto name = design->chip.getBelName(bel);
        ASSERT_EQ(bel, design->chip.getBelByName(name));
        bel_count++;
    }
    ASSERT_EQ(bel_count, 1416);
}

TEST_F(LP1KTest, wire_names)
{
    int wire_count = 0;
    for (auto wire : design->chip.getWires()) {
        auto name = design->chip.getWireName(wire);
        assert(wire == design->chip.getWireByName(name));
        wire_count++;
    }
    ASSERT_EQ(wire_count, 27682);
}

TEST_F(LP1KTest, pip_names)
{
    int pip_count = 0;
    for (auto pip : design->chip.getPips()) {
        auto name = design->chip.getPipName(pip);
        assert(pip == design->chip.getPipByName(name));
        pip_count++;
    }
    ASSERT_EQ(pip_count, 319904);
}

TEST_F(LP1KTest, uphill_to_downhill)
{
    for (auto dst : design->chip.getWires()) {
        for (auto uphill_pip : design->chip.getPipsUphill(dst)) {
            bool found_downhill = false;
            for (auto downhill_pip : design->chip.getPipsDownhill(
                         design->chip.getPipSrcWire(uphill_pip))) {
                if (uphill_pip == downhill_pip) {
                    ASSERT_FALSE(found_downhill);
                    found_downhill = true;
                }
            }
            ASSERT_TRUE(found_downhill);
        }
    }
}

TEST_F(LP1KTest, downhill_to_uphill)
{
    for (auto dst : design->chip.getWires()) {
        for (auto downhill_pip : design->chip.getPipsDownhill(dst)) {
            bool found_uphill = false;
            for (auto uphill_pip : design->chip.getPipsUphill(
                         design->chip.getPipDstWire(downhill_pip))) {
                if (uphill_pip == downhill_pip) {
                    ASSERT_FALSE(found_uphill);
                    found_uphill = true;
                }
            }
            ASSERT_TRUE(found_uphill);
        }
    }
}
