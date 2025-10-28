#pragma once

#include <unordered_map>
#include <string>

#include "item-prop-enum.h"

// Used in spl-damage.cc for lightning rod damage calculations
const int LIGHTNING_CHARGE_MULT = 100;
const int LIGHTNING_MAX_CHARGE = 4;

struct recharge_messages
{
    string noisy;
    string silent;
};

struct evoker_data
{
    const char * key;
    const char * plus;
    int charge_xp_debt;
    int max_charges;
    recharge_messages recharge_msg;
};

static const unordered_map<misc_item_type, evoker_data, std::hash<int>> xp_evoker_data = {
    { MISC_PHIAL_OF_FLOODS, { "phial_debt", "phial_plus", 180, 1,
        { "You hear a faint sloshing from %s as it returns to readiness.",
          "Water glimmers in %s, now refilled and ready to use.", },
    }},
    { MISC_HORN_OF_GERYON, { "horn_debt", "horn_plus", 100, 1 } },
    { MISC_LIGHTNING_ROD,  { "rod_debt", "rod_plus", 80, LIGHTNING_MAX_CHARGE } },
    { MISC_TIN_OF_TREMORSTONES, { "tin_debt", "tin_plus", 110, 2 } },
    { MISC_PHANTOM_MIRROR, { "mirror_debt", "mirror_plus", 250, 1 } },
    { MISC_BOX_OF_BEASTS, { "box_debt", "box_plus", 190, 1 } },
    { MISC_SACK_OF_SPIDERS, { "sack_debt", "sack_plus", 190, 1,
        { "You hear chittering from %s. It's ready.",
          "%s twitches, refilled and ready to use.", },
    }},
    { MISC_CONDENSER_VANE, { "condenser_debt", "condenser_plus", 150, 1 } },
    { MISC_GRAVITAMBOURINE, { "tambourine_debt", "tambourine_plus", 220, 2,
        { "%s jingles faintly as it regains its power.",
          "%s shakes itself petulantly as it silently regains its power." },
    }},
    { MISC_DUNGEON_ATLAS, { "atlas_debt", "atlas_plus", 250, 2 } },
    { MISC_HARP_OF_HEALING, { "harp_debt", "harp_plus", 140, 1 } },
    { MISC_MAGES_CHALICE, { "chalice_debt", "chalice_plus", 150, 1 } },
    { MISC_BUTTERFLY_JAR, { "jar_debt", "jar_plus", 140, 1 } },
    { MISC_PURPLE_STATUETTE, { "purple_debt", "purple_plus", 120, 1 } },
    { MISC_MAGNET, { "magnet_debt", "magnet_plus", 160, 3 } },
    { MISC_LANTERN_OF_SHADOWS, { "lantern_debt", "lantern_plus", 110, 1 } },
    { MISC_SKELETON_KEY, { "skeleton_debt", "skeleton_plus", 400, 3 } },
    { MISC_PANDEMONIUM_PIZZA, { "pizza_debt", "pizza_plus", 400, 4 } },
    { MISC_JUMPER_CABLE, { "cable_debt", "cable_plus", 100, 1 } },
    { MISC_LAMP_OF_IMMOLATION, { "lamp_debt", "lamp_plus", 150, 2 } },
    { MISC_ACID_CAULDRON, { "cauldron_debt", "cauldron_plus", 180, 2 } },
    { MISC_AMULET_OF_RESISTANCE, { "amulet_debt", "amulet_plus", 180, 1 } },
    { MISC_STAFF_OF_LIGHT, { "staff_debt", "staff_plus", 160, 2 } },
    { MISC_BEGINNER_GUIDE, { "guide_debt", "guide_plus", 300, 1 } },
    { MISC_HASTE_RUNESTONE, { "haste_debt", "haste_plus", 250, 1 } },
    { MISC_MEAT_BONE, { "meat_debt", "meat_plus", 150, 1 } },
    { MISC_CRYSTAL_ANVIL, { "anvil_debt", "anvil_plus", 330, 1 } },
};
