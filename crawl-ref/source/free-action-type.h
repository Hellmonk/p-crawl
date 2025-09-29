#pragma once

#include "tag-version.h"

enum free_action_type
{
    FACT_NONE,
    FACT_MELEE,
    FACT_MOVE,
    FACT_SPELL,
    FACT_SWIM,
    FACT_VARIABLE, // sometimes gets a free action, sometimes not (monster only)
    FACT_ANY,

    NUM_FREE_ACTIONS,
};
