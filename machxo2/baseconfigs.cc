#include "config.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN
namespace BaseConfigs {
void config_empty_lcmxo2_256(ChipConfig &cc)
{
    cc.tiles["PT1:CFG0_ENDL"].add_unknown(5, 41);
    cc.tiles["PT1:CFG0_ENDL"].add_unknown(5, 43);
    cc.tiles["PT1:CFG0_ENDL"].add_unknown(5, 47);

    cc.tiles["PT4:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo2_640(ChipConfig &cc)
{
    cc.tiles["EBR_R0C14:EBR1_640"].add_unknown(0, 12);
    cc.tiles["EBR_R0C17:EBR1_640"].add_unknown(0, 12);

    cc.tiles["PT1:CFG0_ENDL"].add_unknown(5, 41);
    cc.tiles["PT1:CFG0_ENDL"].add_unknown(5, 43);
    cc.tiles["PT1:CFG0_ENDL"].add_unknown(5, 47);

    cc.tiles["PT4:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo2_1200(ChipConfig &cc)
{
    cc.tiles["EBR_R6C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C15:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C18:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C21:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo2_2000(ChipConfig &cc)
{
    cc.tiles["EBR_R8C3:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C6:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C9:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C12:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C16:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C19:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C25:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo2_4000(ChipConfig &cc)
{
    cc.tiles["EBR_R11C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C19:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C25:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C28:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C31:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo2_7000(ChipConfig &cc)
{
    cc.tiles["EBR_R13C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C17:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C25:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C28:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C31:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C34:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C37:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C40:EBR1"].add_unknown(0, 12);

    cc.tiles["EBR_R20C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C17:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C25:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C28:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C31:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C34:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C37:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C40:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo3_1300(ChipConfig &cc)
{
    cc.tiles["EBR_R6C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C15:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C18:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R6C21:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo3_2100(ChipConfig &cc)
{
    cc.tiles["EBR_R8C3:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C6:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C9:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C12:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C16:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C19:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C25:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo3_4300(ChipConfig &cc)
{
    cc.tiles["EBR_R11C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C19:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C25:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C28:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R11C31:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo3_6900(ChipConfig &cc)
{
    cc.tiles["EBR_R13C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C17:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C25:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C28:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C31:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C34:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C37:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R13C40:EBR1"].add_unknown(0, 12);

    cc.tiles["EBR_R20C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C17:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C22:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C25:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C28:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C31:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C34:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C37:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R20C40:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}

void config_empty_lcmxo3_9400(ChipConfig &cc)
{
    cc.tiles["EBR_R15C2:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C5:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C8:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C11:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C14:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C17:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C20:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C23:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C27:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C30:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C33:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C36:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C39:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C42:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C45:EBR1_10K"].add_unknown(0, 12);
    cc.tiles["EBR_R15C48:EBR1_10K"].add_unknown(0, 12);

    cc.tiles["EBR_R8C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C17:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C20:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C23:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C27:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C30:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C33:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C36:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C39:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C42:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C45:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R8C48:EBR1"].add_unknown(0, 12);

    cc.tiles["EBR_R22C2:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C5:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C8:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C11:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C14:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C17:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C20:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C23:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C27:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C30:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C33:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C36:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C39:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C42:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C45:EBR1"].add_unknown(0, 12);
    cc.tiles["EBR_R22C48:EBR1"].add_unknown(0, 12);

    cc.tiles["PT4:CFG0"].add_unknown(5, 30);
    cc.tiles["PT4:CFG0"].add_unknown(5, 32);
    cc.tiles["PT4:CFG0"].add_unknown(5, 36);

    cc.tiles["PT7:CFG3"].add_unknown(5, 18);
}
} // namespace BaseConfigs
NEXTPNR_NAMESPACE_END
