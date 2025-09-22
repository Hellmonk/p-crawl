/**
 * @file
 * @brief Spell casting and miscast functions.
**/

#include "AppHdr.h"

#include "spl-cast.h"

#include <iomanip>
#include <sstream>
#include <cmath>

#include "act-iter.h"
#include "areas.h"
#include "art-enum.h"
#include "beam.h"
#include "chardump.h"
#include "cloud.h"
#include "colour.h"
#include "coordit.h"
#include "database.h"
#include "describe.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "evoke.h"
#include "exercise.h"
#include "format.h"
#include "god-abil.h"
#include "god-conduct.h"
#include "god-item.h"
#include "god-passive.h" // passive_t::shadow_spells
#include "hints.h"
#include "items.h"
#include "item-prop.h"
#include "item-use.h"
#include "libutil.h"
#include "macro.h"
#include "menu.h"
#include "melee-attack.h"
#include "message.h"
#include "misc.h"
#include "mon-behv.h"
#include "mon-cast.h"
#include "mon-explode.h"
#include "mon-place.h"
#include "mon-project.h"
#include "mon-util.h"
#include "mutation.h"
#include "options.h"
#include "ouch.h"
#include "output.h"
#include "player.h"
#include "player-stats.h"
#include "prompt.h"
#include "religion.h"
#include "shout.h"
#include "skills.h"
#include "spl-book.h"
#include "spl-clouds.h"
#include "spl-damage.h"
#include "spl-goditem.h"
#include "spl-miscast.h"
#include "spl-monench.h"
#include "spl-other.h"
#include "spl-selfench.h"
#include "spl-summoning.h"
#include "spl-transloc.h"
#include "spl-zap.h"
#include "state.h"
#include "stepdown.h"
#include "stringutil.h"
#include "tag-version.h"
#include "target.h"
#include "teleport.h"
#include "terrain.h"
#include "tilepick.h"
#include "timed-effects.h"
#include "transform.h"
#include "unicode.h"
#include "unwind.h"
#include "view.h"
#include "viewchar.h" // stringize_glyph

static int _spell_enhancement(spell_type spell);
static int _apply_enhancement(const int initial_power,
                              const int enhancer_levels);

void surge_power(const int enhanced)
{
    if (enhanced)               // one way or the other {dlb}
    {
        const string modifier = (enhanced  < -2) ? "extraordinarily" :
                                (enhanced == -2) ? "extremely" :
                                (enhanced ==  2) ? "strong" :
                                (enhanced  >  2) ? "huge"
                                                 : "";
        mprf("You feel %s %s",
             !modifier.length() ? "a"
                                : article_a(modifier).c_str(),
             (enhanced < 0) ? "numb sensation."
                            : "surge of power!");
    }
}

void surge_power_wand(const int mp_cost)
{
    if (mp_cost)
    {
        const bool slight = mp_cost < 3;
        mprf("You feel a %ssurge of power%s",
             slight ? "slight " : "",
             slight ? "."      : "!");
    }
}

static string _spell_base_description(spell_type spell, bool transient)
{
    ostringstream desc;

    int highlight =  spell_highlight_by_utility(spell, COL_UNKNOWN, transient);
    if (you.duration[DUR_ENKINDLED] && highlight == COL_UNKNOWN
        && spell_can_be_enkindled(spell))
    {
        highlight = LIGHTCYAN;
    }

    desc << "<" << colour_to_str(highlight) << ">" << left;

    // spell name
    desc << chop_string(spell_title(spell), 32);

    // spell schools
    desc << spell_schools_string(spell);

    const int so_far = strwidth(desc.str()) - (strwidth(colour_to_str(highlight))+2);
    if (so_far < 58)
        desc << string(58 - so_far, ' ');

    // spell level
    desc << spell_difficulty(spell);
    // XXX: This exact padding is needed to make the webtiles spell menu not re-align
    //      itself whenever we toggle display modes. For some reason, this doesn't
    //      seem to matter for local tiles. Who know why?
    desc << string(2, ' ');
    desc << "</" << colour_to_str(highlight) <<">";

    return desc.str();
}

static string _spell_extra_description(spell_type spell, bool transient)
{
    ostringstream desc;

    int highlight =  spell_highlight_by_utility(spell, COL_UNKNOWN, transient);
    if (you.duration[DUR_ENKINDLED] && spell_can_be_enkindled(spell) && highlight == COL_UNKNOWN)
        highlight = LIGHTCYAN;

    desc << "<" << colour_to_str(highlight) << ">" << left;

    // spell name
    desc << chop_string(spell_title(spell), 32);

    // spell power, spell range, noise
    const string rangestring = spell_range_string(spell);
    const string damagestring = spell_damage_string(spell, false, -1, true);

    desc << chop_string(spell_power_string(spell), 10)
         << chop_string(damagestring.length() ? damagestring : "N/A", 10)
         << chop_string(rangestring, 8)
         << chop_string(spell_noise_string(spell, 10), 14);

    desc << "</" << colour_to_str(highlight) <<">";

    return desc.str();
}

class SpellMenuEntry : public ToggleableMenuEntry
{
public:
    SpellMenuEntry(const string &txt,
                   const string &alt_txt,
                   MenuEntryLevel lev,
                   int qty, int hotk)
        : ToggleableMenuEntry(txt, alt_txt, lev, qty, hotk)
    {
    }

    bool preselected = false;
protected:
    virtual string _get_text_preface() const override
    {
        if (preselected)
            return make_stringf(" %s + ", keycode_to_name(hotkeys[0]).c_str());
        return ToggleableMenuEntry::_get_text_preface();
    }
};

class SpellMenu : public ToggleableMenu
{
public:
    SpellMenu()
        : ToggleableMenu(MF_SINGLESELECT | MF_ANYPRINTABLE
            | MF_NO_WRAP_ROWS | MF_ALLOW_FORMATTING
            | MF_ARROWS_SELECT | MF_INIT_HOVER) {}
protected:
    command_type get_command(int keyin) override
    {
        if (keyin == '?')
            return CMD_MENU_HELP;
        return ToggleableMenu::get_command(keyin);
    }

    bool process_command(command_type cmd) override
    {
        if (cmd == CMD_MENU_HELP)
        {
            int idx = last_hovered;
            if (idx >= 0 && idx < static_cast<int>(items.size()))
            {
                examine_index(idx);
                return true;
            }
        }

        get_selected(&sel);
        // if there's a preselected item, and no current selection, select it.
        // for arrow selection, the hover starts on the preselected item so no
        // special handling is needed.
        if (menu_action == ACT_EXECUTE && cmd == CMD_MENU_SELECT
            && !(flags & MF_ARROWS_SELECT) && sel.empty())
        {
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (static_cast<SpellMenuEntry*>(items[i])->preselected)
                {
                    select_index(i, 1);
                    break;
                }
            }
        }
        return ToggleableMenu::process_command(cmd);
    }

    bool examine_index(int i) override
    {
        ASSERT(i >= 0 && i < static_cast<int>(items.size()));
        if (items[i]->hotkeys.size())
            describe_spell(get_spell_by_letter(items[i]->hotkeys[0]), nullptr);
        return true;
    }
};

// selector is a boolean function that filters spells according
// to certain criteria. Currently used for Tiles to distinguish
// spells targeted on player vs. spells targeted on monsters.
int list_spells(bool toggle_with_I, bool transient, bool viewing,
                bool allow_preselect, const string &action)
{
    if (toggle_with_I && get_spell_by_letter('I') != SPELL_NO_SPELL)
        toggle_with_I = false;

    const string real_action = viewing ? "describe" : action;

    SpellMenu spell_menu;
    const string titlestring = make_stringf("%-25.25s",
            make_stringf("Your spells (%s)", real_action.c_str()).c_str());

    {
        ToggleableMenuEntry* me =
            new ToggleableMenuEntry(
                titlestring + "           Type                      Level  ",
                titlestring + "           Power     Damage    Range   Noise         ",
                MEL_TITLE);
        spell_menu.set_title(me, true, true);
    }
    spell_menu.set_highlighter(nullptr);
    spell_menu.set_tag("spell");
    // TODO: add toggling to describe mode with `?`, add help string, etc...
    spell_menu.add_toggle_from_command(CMD_MENU_CYCLE_MODE);
    spell_menu.add_toggle_from_command(CMD_MENU_CYCLE_MODE_REVERSE);

    string more_str = make_stringf("<lightgrey>Select a spell to %s</lightgrey>",
        real_action.c_str());
    more_str = pad_more_with_esc(more_str + "   [<w>?</w>] help");
    string toggle_desc = menu_keyhelp_cmd(CMD_MENU_CYCLE_MODE);
    if (toggle_with_I)
    {
        // why `I`?
        spell_menu.add_toggle_key('I');
        toggle_desc += "/[<w>I</w>]";
    }
    toggle_desc += " toggle spell headers";
    more_str = pad_more_with(more_str, toggle_desc);
    spell_menu.set_more(formatted_string::parse_string(more_str));
    // TODO: should allow toggling between execute and examine
    spell_menu.menu_action = viewing ? Menu::ACT_EXAMINE : Menu::ACT_EXECUTE;

    int initial_hover = 0;
    for (int i = 0; i < 52; ++i)
    {
        const char letter = index_to_letter(i);
        const spell_type spell = get_spell_by_letter(letter);

        if (!is_valid_spell(spell))
            continue;

        SpellMenuEntry* me =
            new SpellMenuEntry(_spell_base_description(spell, transient),
                               _spell_extra_description(spell, transient),
                               MEL_ITEM, 1, letter);
        me->colour = spell_highlight_by_utility(spell, COL_UNKNOWN, transient);
        // TODO: maybe fill this from the quiver if there's a quivered spell and
        // no last cast one?
        if (allow_preselect && you.last_cast_spell == spell)
        {
            initial_hover = i;
            me->preselected = true;
        }

        me->add_tile(tile_def(tileidx_spell(spell)));
        spell_menu.add_entry(me);
    }
    spell_menu.set_hovered(initial_hover);

    int choice = 0;
    spell_menu.on_single_selection = [&choice](const MenuEntry& item)
    {
        ASSERT(item.hotkeys.size() == 1);
        choice = item.hotkeys[0];
        return false;
    };

    spell_menu.show();
    if (!crawl_state.doing_prev_cmd_again)
    {
        redraw_screen();
        update_screen();
    }
    return choice;
}

// Effects that happen after spells which are otherwise simple zaps.
static void _apply_post_zap_effect(spell_type spell, coord_def target)
{
    switch (spell)
    {
    case SPELL_KISS_OF_DEATH:
        drain_player(100, true, true);
        break;
    case SPELL_BOMBARD:
        if (coinflip())
            you.stumble_away_from(target, "the blast");
        break;
    default:
        break;
    }
}

static int _skill_power(spell_type spell)
{
    // spell school average *2, plus spellcasting
    int power = 0;
    const spschools_type disciplines = get_spell_disciplines(spell);
    const int skillcount = count_bits(disciplines);
    if (skillcount)
    {
        for (const auto bit : spschools_type::range())
            if (disciplines & bit)
                power += you.skill(spell_type2skill(bit), 2);
        power /= skillcount;
    }
    return power + you.skill(SK_SPELLCASTING);
}

/*
 * Calculate spell power.
 *
 * @param spell         the spell to check
 *
 * @return the resulting spell power.
 */
int calc_spell_power(spell_type spell)
{
    int power = _skill_power(spell);

    if (you.divine_exegesis)
        power += you.skill(SK_INVOCATIONS);

    power = (power * you.intel()) / 10;

    // Wild magic boosts spell power, subdued magic decreases it.
    power += you.get_mutation_level(MUT_WILD_MAGIC);
    power -= you.get_mutation_level(MUT_SUBDUED_MAGIC);

    // Augmentation boosts spell power at high HP.
    power += augmentation_amount();

    // Enhancers boost / deboost by 3 power apiece
    power = _apply_enhancement(power, _spell_enhancement(spell));

    // Each level of horror reduces spellpower by 1
    if (you.duration[DUR_HORROR])
        power -= 1 * you.props[HORROR_PENALTY_KEY].get_int();

    if (you.duration[DUR_ENKINDLED] && spell_can_be_enkindled(spell))
        power = (power + (you.experience_level)) * 3 / 2;

    const int cap = spell_power_cap(spell);
    if (cap > 0)
        power = min(power, cap);

    // post-cap, so the result is more predictable to the player.
    if (you.duration[DUR_DIMINISHED_SPELLS])
        power = power / 2;

    return max(0, power);
}

static int _spell_enhancement(spell_type spell)
{
    const spschools_type typeflags = get_spell_disciplines(spell);
    int enhanced = 0;

    if (typeflags & spschool::hexes)
        enhanced += player_spec_hex();

    if (typeflags & spschool::summoning)
        enhanced += player_spec_summ();

    if (typeflags & spschool::necromancy)
        enhanced += player_spec_death();

    if (typeflags & spschool::translocation)
        enhanced += player_spec_tloc();

    if (typeflags & spschool::fire)
        enhanced += player_spec_fire();

    if (typeflags & spschool::ice)
        enhanced += player_spec_cold();

    if (typeflags & spschool::earth)
        enhanced += player_spec_earth();

    if (typeflags & spschool::air)
        enhanced += player_spec_air();

    if (you.unrand_equipped(UNRAND_BATTLE))
    {
        if (vehumet_supports_spell(spell))
            enhanced++;
        else
            enhanced--;
    }

    enhanced += you.archmagi();
    enhanced += you.duration[DUR_BRILLIANCE] > 0
                || you.unrand_equipped(UNRAND_FOLLY);

    return enhanced;
}

/**
 * Apply the effects of spell enhancers (and de-enhancers) on spellpower.
 *
 * @param initial_power     The power of the spell before enhancers are added.
 * @param enhancer_levels   The number of enhancements levels to apply.
 * @return                  The power of the spell with enhancers considered.
 */
static int _apply_enhancement(const int initial_power,
                              const int enhancer_levels)
{
    int power = initial_power;

    return power + enhancer_levels * 3;
}

void inspect_spells()
{
    if (!you.spell_no)
    {
        canned_msg(MSG_NO_SPELLS);
        return;
    }

    list_spells(true, false, true);
}

/**
 * Can the player cast any spell at all? Checks for things that limit
 * spellcasting regardless of the specific spell we want to cast.
 *
 * @param quiet    If true, don't print a reason why no spell can be cast.
 * @return True if we could cast a spell, false otherwise.
*/
bool can_cast_spells(bool quiet)
{
    if (!get_form()->can_cast)
    {
        if (!quiet)
            canned_msg(MSG_PRESENT_FORM);
        return false;
    }

    if (you.duration[DUR_WATER_HOLD] && !you.res_water_drowning())
    {
        if (!quiet)
            mpr("You cannot cast spells while unable to breathe!");
        return false;
    }

    if (you.duration[DUR_NO_CAST])
    {
        if (!quiet)
            mpr("You are unable to access your magic!");
        return false;
    }

    // Randart weapons.
    if (you.no_cast())
    {
        if (!quiet)
            mpr("Something interferes with your magic!");
        return false;
    }

    if (you.berserk())
    {
        if (!quiet)
            canned_msg(MSG_TOO_BERSERK);
        return false;
    }

    if (you.confused())
    {
        if (!quiet)
            mpr("You're too confused to cast spells.");
        return false;
    }

    if (silenced(you.pos()))
    {
        if (!quiet)
            mpr("You cannot cast spells when silenced!");
        // included in default force_more_message
        return false;
    }

    return true;
}

void do_cast_spell_cmd(bool force)
{
    if (cast_a_spell(!force) == spret::abort)
        flush_input_buffer(FLUSH_ON_FAILURE);
}

static void _handle_channelling(int cost, spret cast_result)
{
    if (you.has_mutation(MUT_HP_CASTING) || cast_result == spret::abort)
        return;

    const int sources = player_channelling();
    if (!sources)
        return;

    // Miscasts always get refunded, successes only sometimes do.
    if (cast_result != spret::fail && !x_chance_in_y(sources, 4))
        return;

    mpr("Magical energy flows into your mind!");
    inc_mp(cost, true);
    did_god_conduct(DID_WIZARDLY_ITEM, 10);
}

/**
 * Cast a spell.
 *
 * Handles general preconditions & costs.
 *
 * @param check_range   If true, abort if no targets are in range. (z vs Z)
 * @param spell         The type of spell to be cast.
 * @param force_failure True if the spell's failure has already been determined
 *                      in advance (for spells being cast via Divine Exegesis).
 * @return              Whether the spell was successfully cast.
 **/
spret cast_a_spell(bool check_range, spell_type spell, dist *_target,
                   bool force_failure)
{
    // If you don't have any spells memorized (and aren't using exegesis),
    // you can't cast any spells. Simple as.
    if (!you.spell_no && !you.divine_exegesis)
    {
        canned_msg(MSG_NO_SPELLS);
        return spret::abort;
    }

    if (!can_cast_spells())
    {
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    if (crawl_state.game_is_hints())
        Hints.hints_spell_counter++;

    if (spell == SPELL_NO_SPELL)
    {
        int keyin = 0;

        string luachoice;
        if (!clua.callfn("c_choose_spell", ">s", &luachoice))
        {
            if (!clua.error.empty())
                mprf(MSGCH_ERROR, "Lua error: %s", clua.error.c_str());
        }
        else if (!luachoice.empty() && isalpha(luachoice[0]))
        {
            keyin = luachoice[0];
            const spell_type spl = get_spell_by_letter(keyin);

            // Bad entry from lua, defer to the user
            if (!is_valid_spell(spl))
                keyin = 0;
        }

        while (true)
        {
            if (keyin == 0 && !Options.spell_menu)
            {
                if (you.spell_no == 1)
                {
                    // Set last_cast_spell to the current only spell.
                    for (int i = 0; i < 52; ++i)
                    {
                        const char letter = index_to_letter(i);
                        const spell_type spl = get_spell_by_letter(letter);

                        if (!is_valid_spell(spl))
                            continue;

                        you.last_cast_spell = spl;
                        break;
                    }
                }

                // We allow setting last cast spell by Divine Exegesis, but we
                // don't allow recasting it with the UI unless we actually have
                // the spell memorized.
                if (you.last_cast_spell != SPELL_NO_SPELL
                    && !you.has_spell(you.last_cast_spell))
                {
                    you.last_cast_spell = SPELL_NO_SPELL;
                }

                if (you.last_cast_spell == SPELL_NO_SPELL
                    || !Options.enable_recast_spell)
                {
                    mprf(MSGCH_PROMPT, "Cast which spell? (? or * to list) ");
                }
                else
                {
                    mprf(MSGCH_PROMPT, "Casting: <w>%s</w>",
                                       spell_title(you.last_cast_spell));
                    mprf(MSGCH_PROMPT, "Confirm with . or Enter, or press "
                                       "? or * to list all spells.");
                }

                keyin = numpad_to_regular(get_ch());
            }

            if (keyin == '?' || keyin == '*' || Options.spell_menu)
            {
                keyin = list_spells(true, true, false);
                if (!keyin)
                    keyin = ESCAPE;

                if (!crawl_state.doing_prev_cmd_again)
                {
                    redraw_screen();
                    update_screen();
                }

                if (isaalpha(keyin) || key_is_escape(keyin))
                    break;
                else
                    clear_messages();

                keyin = 0;
            }
            else
                break;
        }

        if (key_is_escape(keyin))
        {
            canned_msg(MSG_OK);
            crawl_state.zero_turns_taken();
            return spret::abort;
        }
        else if (Options.enable_recast_spell
                 && (keyin == '.' || keyin == CK_ENTER))
        {
            spell = you.last_cast_spell;
        }
        else if (!isaalpha(keyin))
        {
            mpr("You don't know that spell.");
            crawl_state.zero_turns_taken();
            return spret::abort;
        }
        else
            spell = get_spell_by_letter(keyin);
    }

    if (spell == SPELL_NO_SPELL)
    {
        mpr("You don't know that spell.");
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    // MP, confusion, Ru sacs
    const auto reason = casting_uselessness_reason(spell, true);
    if (!reason.empty())
    {
        mpr(reason);
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    if (check_range && spell_no_hostile_in_range(spell))
    {
        // Abort if there are no hostiles within range, but flash the range
        // markers for a short while.
        mpr("You can't see any susceptible monsters within range! "
            "(Use <w>Z</w> to cast anyway.)");

        if ((Options.use_animations & UA_RANGE) && Options.darken_beyond_range)
        {
            targeter_smite range(&you, calc_spell_range(spell), 0, 0, false, true);
            range_view_annotator show_range(&range);
            delay(50);
        }
        crawl_state.zero_turns_taken();
        return spret::abort;
    }

    if (god_punishes_spell(spell, you.religion)
        && !crawl_state.disables[DIS_CONFIRMATIONS])
    {
        // None currently dock just piety, right?
        if (!yesno("Casting this spell will place you under penance. "
                   "Really cast?", true, 'n'))
        {
            canned_msg(MSG_OK);
            crawl_state.zero_turns_taken();
            return spret::abort;
        }
    }

    you.last_cast_spell = spell;
    // Silently take MP before the spell.
    const int cost = spell_mana(spell);
    pay_mp(cost);

    const spret cast_result = your_spells(spell, 0, !you.divine_exegesis,
                                          nullptr, _target, force_failure);
    if (cast_result == spret::abort
        || you.divine_exegesis && cast_result == spret::fail)
    {
        if (cast_result == spret::abort)
            crawl_state.zero_turns_taken();
        // Return the MP since the spell is aborted.
        refund_mp(cost);

        redraw_screen();
        update_screen();
        return cast_result;
    }

    practise_casting(spell, cast_result == spret::success);
    _handle_channelling(cost, cast_result);
    if (cast_result == spret::success)
    {
        did_god_conduct(DID_SPELL_CASTING, 1 + random2(5));
        count_action(CACT_CAST, spell);
    }

    finalize_mp_cost(0);
    // Check if an HP payment brought us low enough
    // to trigger Celebrant or time-warped blood.
    makhleb_celebrant_bloodrite();
    _maybe_blood_hastes_allies();
    you.turn_is_over = true;
    alert_nearby_monsters();

    return cast_result;
}

/**
 * Handles divine response to spellcasting.
 *
 * @param spell         The type of spell just cast.
 */
static void _spellcasting_god_conduct(spell_type spell)
{
    // If you are casting while a god is acting, then don't do conducts.
    // (Presumably Xom is forcing you to cast a spell.)
    if (crawl_state.is_god_acting())
        return;

    const int conduct_level = 10 + spell_difficulty(spell);

    if (is_evil_spell(spell) || you.spellcasting_unholy())
        did_god_conduct(DID_EVIL, conduct_level);

    if (is_unclean_spell(spell))
        did_god_conduct(DID_UNCLEAN, conduct_level);

    if (is_chaotic_spell(spell))
        did_god_conduct(DID_CHAOS, conduct_level);

    // not is_hasty_spell since the other ones handle the conduct themselves.
    if (spell == SPELL_SWIFTNESS)
        did_god_conduct(DID_HASTY, conduct_level);
}

/**
 * Handles side effects of successfully casting a spell.
 *
 * Spell noise, magic 'sap' effects, and god conducts.
 *
 * @param spell         The type of spell just cast.
 * @param god           Which god is casting the spell; NO_GOD if it's you.
 * @param fake_spell    true if the spell is evoked or from an innate or divine ability
 *                      false if it is a spell being cast normally.
 */
static void _spellcasting_side_effects(spell_type spell, god_type god,
                                       bool fake_spell)
{
    _spellcasting_god_conduct(spell);

    if (god == GOD_NO_GOD)
    {
        if (you.duration[DUR_SAP_MAGIC] && !fake_spell)
        {
            mprf(MSGCH_WARN, "You lose access to your magic!");
            you.increase_duration(DUR_NO_CAST, 3 + random2(3));
        }

        // Make some noise if it's actually the player casting.
        noisy(spell_noise(spell), you.pos());
    }

    alert_nearby_monsters();

}

#ifdef WIZARD
static void _try_monster_cast(spell_type spell, int /*powc*/,
                              dist &spd, bolt &beam)
{
    if (monster_at(you.pos()))
    {
        mpr("Couldn't try casting monster spell because you're "
            "on top of a monster.");
        return;
    }

    monster* mon = get_free_monster();
    if (!mon)
    {
        mpr("Couldn't try casting monster spell because there is "
            "no empty monster slot.");
        return;
    }

    mpr("Invalid player spell, attempting to cast it as monster spell.");

    mon->mname      = "Dummy Monster";
    mon->type       = MONS_HUMAN;
    mon->behaviour  = BEH_SEEK;
    mon->attitude   = ATT_FRIENDLY;
    mon->flags      = (MF_NO_REWARD | MF_JUST_SUMMONED | MF_SEEN
                       | MF_WAS_IN_VIEW | MF_HARD_RESET);
    mon->hit_points = you.hp;
    mon->set_hit_dice(you.experience_level);
    mon->set_position(you.pos());
    mon->target     = spd.target;
    mon->mid        = MID_PLAYER;

    if (!spd.isTarget)
        mon->foe = MHITNOT;
    else if (!monster_at(spd.target))
    {
        if (spd.isMe())
            mon->foe = MHITYOU;
        else
            mon->foe = MHITNOT;
    }
    else
        mon->foe = env.mgrid(spd.target);

    env.mgrid(you.pos()) = mon->mindex();

    mons_cast(mon, beam, spell, MON_SPELL_NO_FLAGS);

    mon->reset();
}
#endif // WIZARD

static spret _do_cast(spell_type spell, int powc, const dist& spd,
                           bolt& beam, god_type god, bool fail,
                           bool actual_spell);

/**
 * Should this spell be aborted before casting properly starts, either because
 * it can't legally be cast in this circumstance, or because the player opts
 * to cancel it in response to a prompt?
 *
 * @param spell         The spell to be checked
 * @param fake_spell    true if the spell is evoked or from an innate or divine ability
 *                      false if it is a spell being cast normally.
 * @return              Whether the spellcasting should be aborted.
 */
static bool _spellcasting_aborted(spell_type spell)
{
    string msg;

    // casting-general checks (MP etc) are not carried out here
    msg = spell_uselessness_reason(spell, true, true, true);

    if (!msg.empty())
    {
        mpr(msg);
        return true;
    }

    vector<text_pattern> &actions = Options.confirm_action;
    if (!actions.empty())
    {
        const char* name = spell_title(spell);
        for (const text_pattern &action : actions)
        {
            if (!action.matches(name))
                continue;

            string prompt = "Really cast " + string(name) + "?";
            if (!yesno(prompt.c_str(), false, 'n'))
            {
                canned_msg(MSG_OK);
                return true;
            }
            break;
        }
    }

    return false;
}

// this is a crude approximation used for the convenience UI targeter of
// Dragon's call and Manifold Assault
static vector<coord_def> _simple_find_all_hostiles()
{
    vector<coord_def> result;
    for (monster_near_iterator mi(you.pos(), LOS_NO_TRANS); mi; ++mi)
    {
        if (!mons_aligned(&you, *mi)
            && mons_is_threatening(**mi)
            && you.can_see(**mi))
        {
            result.push_back((*mi)->pos());
        }
    }

    return result;
}

// TODO: refactor into target.cc, move custom classes out of target.h
unique_ptr<targeter> find_spell_targeter(spell_type spell, int pow, int range)
{
    switch (spell)
    {
    case SPELL_FIREBALL:
    case SPELL_ICEBLAST:
    case SPELL_FASTROOT:
    case SPELL_WARP_SPACE:
        return make_unique<targeter_beam>(&you, range, spell_to_zap(spell), pow,
                                          1, 1);
    case SPELL_HURL_DAMNATION:
        return make_unique<targeter_beam>(&you, range, ZAP_HURL_DAMNATION, pow,
                                          1, 1);
    case SPELL_HURL_TORCHLIGHT:
        return make_unique<targeter_beam>(&you, range, ZAP_HURL_TORCHLIGHT, pow,
                                          1, 1);
    case SPELL_MEPHITIC_CLOUD:
        return make_unique<targeter_beam>(&you, range, ZAP_MEPHITIC, pow,
                                          pow >= 100 ? 1 : 0, 1);
    case SPELL_RUST_BREATH:
        return make_unique<targeter_beam>(&you, 4, ZAP_RUST_BREATH, pow,
                                          pow >= 25 ? 2 : 1, 2);
    case SPELL_FIRE_STORM:
        return make_unique<targeter_smite>(&you, range, 2, pow > 76 ? 3 : 2);
    case SPELL_FREEZING_CLOUD:
    case SPELL_POISONOUS_CLOUD:
    case SPELL_HOLY_BREATH:
        return make_unique<targeter_cloud>(&you, spell_to_cloud(spell), range);
    case SPELL_THUNDERBOLT:
        return make_unique<targeter_thunderbolt>(&you, range,
                   get_thunderbolt_last_aim(&you));
    case SPELL_LRD:
        return make_unique<targeter_fragment>(&you, pow, range);
    case SPELL_AIRSTRIKE:
        return make_unique<targeter_airstrike>();
    case SPELL_MOMENTUM_STRIKE:
    case SPELL_DIMENSIONAL_BULLSEYE:
        return make_unique<targeter_smite>(&you, range);
    case SPELL_FULMINANT_PRISM:
        return make_unique<targeter_smite>(&you, range, 0, 2);
    case SPELL_GLACIATE:
        return make_unique<targeter_cone>(&you, range);
    case SPELL_GRAVITAS:
        return make_unique<targeter_smite>(&you, range, gravitas_radius(pow),
                                                        gravitas_radius(pow));
    case SPELL_VIOLENT_UNRAVELLING:
        return make_unique<targeter_unravelling>();
    case SPELL_INFESTATION:
        return make_unique<targeter_smite>(&you, range, 2, 2, true, false, true,
                                           [](const coord_def& p) -> bool {
                                              return you.pos() != p; });
    case SPELL_PASSWALL:
        return make_unique<targeter_passwall>(range);
    case SPELL_DIG:
        return make_unique<targeter_dig>(range);
    case SPELL_ELECTRIC_CHARGE:
        return make_unique<targeter_charge>(&you, range);

    // untargeted spells -- everything beyond here is a static targeter
    case SPELL_HAILSTORM:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS, range, 0, 2);
    case SPELL_ISKENDERUNS_MYSTIC_BLAST:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, range, 0, 1);
    case SPELL_STARBURST:
        return make_unique<targeter_starburst>(&you, range, pow);
    case SPELL_IRRADIATE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 1, 0, 1);
    case SPELL_DISCHARGE: // not entirely accurate...maybe should highlight
                          // all potentially affected monsters?
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 1);
    case SPELL_ARCJOLT:
        return make_unique<targeter_multiposition>(&you,
                                                   arcjolt_targets(you, false));
    case SPELL_PLASMA_BEAM:
    {
        auto plasma_targets = plasma_beam_targets(you, pow, false);
        auto plasma_paths = plasma_beam_paths(you.pos(), plasma_targets);
        const aff_type a = plasma_targets.size() == 1 ? AFF_YES : AFF_MAYBE;
        return make_unique<targeter_multiposition>(&you, plasma_paths, a);
    }
    case SPELL_PILEDRIVER:
        return make_unique<targeter_piledriver>();
    case SPELL_CHAIN_LIGHTNING:
        return make_unique<targeter_chain_lightning>();
    case SPELL_MAXWELLS_COUPLING:
        return make_unique<targeter_maxwells_coupling>();
    case SPELL_FROZEN_RAMPARTS:
        return make_unique<targeter_walls>(&you, find_ramparts_walls());
    case SPELL_DISPERSAL:
    case SPELL_DISJUNCTION:
    case SPELL_GLOOM:
        return make_unique<targeter_maybe_radius>(&you, LOS_SOLID_SEE, range,
                                                  0, 1);
    case SPELL_INNER_FLAME:
        return make_unique<targeter_inner_flame>(&you, range);
    case SPELL_TELEPORT_OTHER:
        return make_unique<targeter_teleport_other>(&you, range);
    case SPELL_SOUL_SPLINTER:
        return make_unique<targeter_soul_splinter>(&you, range);
    case SPELL_SIMULACRUM:
        return make_unique<targeter_simulacrum>(&you, range);
    case SPELL_LEDAS_LIQUEFACTION:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS,
                                            liquefaction_max_range(pow),
                                            0, 0, 1);
    case SPELL_SILENCE:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS,
                                            silence_max_range(pow),
                                            0, 0,
                                            silence_min_range(pow));
    case SPELL_POISONOUS_VAPOURS:
        return make_unique<targeter_poisonous_vapours>(&you, range);
    case SPELL_MERCURY_ARROW:
        return make_unique<targeter_explosive_beam>(&you, ZAP_MERCURY_ARROW, pow,
                                                    range, true, false);
    case SPELL_GRAVE_CLAW:
        return make_unique<targeter_smite>(&you, range);

    // at player's position only but not a selfench
    case SPELL_SUBLIMATION_OF_BLOOD:
    case SPELL_BORGNJORS_REVIVIFICATION:
    case SPELL_BLASTMOTE:
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, 0);

    // LOS radius:
    case SPELL_OZOCUBUS_REFRIGERATION:
        return make_unique<targeter_refrig>(&you);
    case SPELL_OLGREBS_TOXIC_RADIANCE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS,
                                                  LOS_RADIUS, 0, 1);
    case SPELL_POLAR_VORTEX:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS,
                                            POLAR_VORTEX_RADIUS, 0, 1);
    case SPELL_SHATTER:
        return make_unique<targeter_shatter>(&you); // special version that
                                                    // affects walls
    case SPELL_IGNITE_POISON: // many cases
        return make_unique<targeter_ignite_poison>(&you);
    case SPELL_CAUSE_FEAR: // for these, we just mark the eligible monsters
        return make_unique<targeter_fear>();
    case SPELL_ANGUISH:
        return make_unique<targeter_anguish>();
    case SPELL_INTOXICATE:
        return make_unique<targeter_intoxicate>();
    case SPELL_ENGLACIATION:
        return make_unique<targeter_englaciate>();
    case SPELL_DRAIN_LIFE:
        return make_unique<targeter_drain_life>();
    case SPELL_DISCORD:
        return make_unique<targeter_discord>();
    case SPELL_IGNITION:
        return make_unique<targeter_multifireball>(&you,
                   get_ignition_blast_sources(&you, true));

    // Summons. Most summons have a simple range 2 radius, see
    // find_newmons_square
    case SPELL_SUMMON_SMALL_MAMMAL:
    case SPELL_AWAKEN_ARMOUR:
    case SPELL_SUMMON_ICE_BEAST:
    case SPELL_SPHINX_SISTERS:
    case SPELL_SUMMON_CACTUS:
    case SPELL_SUMMON_HYDRA:
    case SPELL_SUMMON_MANA_VIPER:
    case SPELL_CONJURE_BALL_LIGHTNING:
    case SPELL_SHADOW_CREATURES: // used for ?summoning
    case SPELL_FORGE_BLAZEHEART_GOLEM:
    case SPELL_CALL_IMP:
    case SPELL_SUMMON_HORRIBLE_THINGS:
    case SPELL_SPELLSPARK_SERVITOR:
    case SPELL_FORGE_LIGHTNING_SPIRE:
    case SPELL_BATTLESPHERE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 2, 0, 1);
    case SPELL_SUMMON_SEISMOSAURUS_EGG:
    case SPELL_HOARFROST_CANNONADE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 3, 0, 2);
    case SPELL_CALL_CANINE_FAMILIAR:
    {
        const monster* dog = find_canine_familiar();
        if (!dog)
            return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 2, 0, 1);
        vector<coord_def> targ = { dog->pos() };
        return make_unique<targeter_multiposition>(&you, targ);
    }
    case SPELL_SURPRISING_CROCODILE:
        return make_unique<targeter_surprising_crocodile>(&you);
    case SPELL_PLATINUM_PARAGON:
    {
        const monster* paragon = find_player_paragon();
        if (!paragon)
            return make_unique<targeter_paragon_deploy>(range);
        else if (paragon_charge_level(*paragon) == 2)
            return make_unique<targeter_smite>(paragon, 3, 3, 3, true, false, false);
        else
            return make_unique<targeter_smite>(paragon, 3, 2, 2, true, false, false);
    }
    case SPELL_MONARCH_BOMB:
    {
        if (count_summons(&you, SPELL_MONARCH_BOMB) > 1)
            return make_unique<targeter_multiposition>(&you, get_monarch_detonation_spots(you), AFF_YES);
        else
            return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 2, 0, 1);
    }
    case SPELL_FOXFIRE:
        return make_unique<targeter_maybe_radius>(&you, LOS_NO_TRANS, 1, 0, 1);

    case SPELL_MALIGN_GATEWAY:
        return make_unique<targeter_malign_gateway>(you);

    // TODO: this actually has pretty wtf positioning that uses compass
    // directions, so this targeter is not entirely accurate.
    case SPELL_SUMMON_FOREST:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS, LOS_RADIUS,
                                            0, 2);

    case SPELL_BLINK:
        return make_unique<targeter_multiposition>(&you, find_blink_targets());
    case SPELL_MANIFOLD_ASSAULT:
        return make_unique<targeter_multiposition>(&you,
                                                   _simple_find_all_hostiles());
    case SPELL_SCORCH:
        return make_unique<targeter_scorch>(you, range, false);
    case SPELL_DRAGON_CALL: // this is just convenience: you can start the spell
                            // with no enemies in sight
        return make_unique<targeter_multifireball>(&you,
                                                   _simple_find_all_hostiles());
    case SPELL_NOXIOUS_BOG:
        return make_unique<targeter_bog>(&you, pow);
    case SPELL_FLAME_WAVE:
        return make_unique<targeter_flame_wave>(range);
    case SPELL_GOLUBRIAS_PASSAGE:
        return make_unique<targeter_passage>(range);
    case SPELL_SIGIL_OF_BINDING:
        return make_unique<targeter_multiposition>(&you,
                                                   find_sigil_locations(true));
    case SPELL_BOULDER:
        return make_unique<targeter_boulder>(&you, barrelling_boulder_hp(pow));
    case SPELL_PERMAFROST_ERUPTION:
        return make_unique<targeter_permafrost>(you, pow);
    case SPELL_PETRIFY:
        return make_unique<targeter_chain>(&you, range, ZAP_PETRIFY);
    case SPELL_RIMEBLIGHT:
        return make_unique<targeter_chain>(&you, range, ZAP_RIMEBLIGHT);
    case SPELL_COMBUSTION_BREATH:
        return make_unique<targeter_explosive_beam>(&you, ZAP_COMBUSTION_BREATH, pow, range);
    case SPELL_NOXIOUS_BREATH:
        // Note the threshold where it becomes possible to make clouds off the main beam
        return make_unique<targeter_explosive_beam>(&you, ZAP_NOXIOUS_BREATH,
                                                    pow, range, false, pow > 10);
    case SPELL_GALVANIC_BREATH:
        return make_unique<targeter_galvanic>(&you, pow, range);
    case SPELL_NULLIFYING_BREATH:
        return make_unique<targeter_beam>(&you, range, ZAP_NULLIFYING_BREATH, pow,
                                          2, 2);
    case SPELL_GELLS_GAVOTTE:
        return make_unique<targeter_gavotte>(&you);

    case SPELL_MAGNAVOLT:
        return make_unique<targeter_magnavolt>(&you, range);

    case SPELL_HELLFIRE_MORTAR:
        return make_unique<targeter_mortar>(&you, range);

    case SPELL_PUTREFACTION:
        return make_unique<targeter_putrefaction>(range);

    case SPELL_DIAMOND_SAWBLADES:
        return make_unique<targeter_multiposition>(&you,
                                                   diamond_sawblade_spots(false));

    case SPELL_SPLINTERFROST_SHELL:
        return make_unique<targeter_wall_arc>(&you, 4);

    case SPELL_PERCUSSIVE_TEMPERING:
        return make_unique<targeter_tempering>();

    case SPELL_FORTRESS_BLAST:
        return make_unique<targeter_radius>(&you, LOS_NO_TRANS, range);

    case SPELL_SPIKE_LAUNCHER:
    {
        vector<coord_def> walls = find_spike_launcher_walls();
        return make_unique<targeter_multiposition>(&you, walls, walls.size() > 1
                                                                    ? AFF_MAYBE
                                                                    : AFF_YES);
    }

    default:
        break;
    }

    if (spell_to_zap(spell) != NUM_ZAPS)
    {
        return make_unique<targeter_beam>(&you, range, spell_to_zap(spell),
                                          pow, 0, 0);
    }

    // selfench is used mainly for monster AI, so it is a bit over-applied in
    // the spell data
    if (get_spell_flags(spell) & spflag::selfench
        && !spell_typematch(spell, spschool::translocation)) // blink, passage
    {
        return make_unique<targeter_radius>(&you, LOS_SOLID_SEE, 0);
    }

    return nullptr;
}

bool spell_has_targeter(spell_type spell)
{
    return bool(find_spell_targeter(spell, 1, 1));
}

// Returns the nth triangular number.
static int _triangular_number(int n)
{
    return n * (n+1) / 2;
}

// _tetrahedral_number: returns the nth tetrahedral number.
// This is the number of triples of nonnegative integers with sum < n.
static int _tetrahedral_number(int n)
{
    return n * (n+1) * (n+2) / 6;
}

/**
 * Compute success chance for WL-checking spells and abilities.
 *
 * @param wl The willpower of the target.
 * @param powc The enchantment power.
 * @param scale The denominator of the result.
 * @param round_up Should the resulting chance be rounded up (true) or
 *        down (false, the default)?
 *
 * @return The chance, out of scale, that the enchantment affects the target.
 */
int hex_success_chance(const int wl, int powc, int scale, bool round_up)
{
    const int pow = ench_power_stepdown(powc);
    const int target = wl + 100 - pow;
    const int denom = 101 * 100;
    const int adjust = round_up ? denom - 1 : 0;

    if (target <= 0)
        return scale;
    if (target > 200)
        return 0;
    if (target <= 100)
        return (scale * (denom - _triangular_number(target)) + adjust) / denom;
    return (scale * _triangular_number(201 - target) + adjust) / denom;
}

// approximates _test_beam_hit in a deterministic fashion.
static int _to_hit_pct(const monster_info& mi, int acc)
{
    if (acc == AUTOMATIC_HIT)
        return 100;

    acc += mi.lighting_modifiers();
    if (acc <= 1)
        return mi.ev <= 2 ? 100 : 0;

    const int base_ev = mi.ev + (mi.is(MB_REPEL_MSL) ? REPEL_MISSILES_EV_BONUS : 0);

    int hits = 0;
    int iters = 0;
    for (int outer_ev_roll = 0; outer_ev_roll < base_ev; outer_ev_roll++)
    {
        for (int inner_ev_roll_a = 0; inner_ev_roll_a < outer_ev_roll; inner_ev_roll_a++)
        {
            for (int inner_ev_roll_b = 0; inner_ev_roll_b < outer_ev_roll; inner_ev_roll_b++)
            {
                const int ev = (inner_ev_roll_a + inner_ev_roll_b) / 2; // not right but close
                for (int rolled_mhit = 0; rolled_mhit < acc; rolled_mhit++)
                {
                    iters++;
                    if (iters >= 1000000)
                        return -1; // sanity breakout to not kill servers
                    if (rolled_mhit >= ev)
                        hits++;
                }
            }
        }
    }

    int base_chance = 0;
    if (iters <= 0) // probably low monster ev?
        base_chance = 100;
    else
        base_chance = hits * 100 / iters;

    base_chance = base_chance * (100 - player_blind_miss_chance(you.pos().distance_from(mi.pos))) / 100;
    return base_chance;
}

static vector<string> _desc_hit_chance(const monster_info& mi, int acc)
{
    if (!acc)
        return vector<string>{};
    const int hit_pct = _to_hit_pct(mi, acc);
    if (hit_pct == -1)
        return vector<string>{};

    ostringstream result;
    describe_hit_chance(hit_pct, result, nullptr, false,
                        you.pos().distance_from(mi.pos));
    return vector<string>{result.str()};
}

vector<string> desc_beam_hit_chance(const monster_info& mi, targeter* hitfunc)
{
    targeter_beam* beam_hitf = dynamic_cast<targeter_beam*>(hitfunc);
    if (!beam_hitf)
        return vector<string>{};
    return _desc_hit_chance(mi, beam_hitf->beam.hit);
}

static vector<string> _desc_plasma_hit_chance(const monster_info& mi, int powc)
{
    bolt beam;
    zappy(spell_to_zap(SPELL_PLASMA_BEAM), powc, false, beam);
    const int hit_pct = _to_hit_pct(mi, beam.hit);
    if (hit_pct == -1)
        return vector<string>{};

    ostringstream result;
    describe_hit_chance(hit_pct, result, nullptr, false,
                        you.pos().distance_from(mi.pos));
    return vector<string>{make_stringf("2x%s", result.str().c_str())};
}

static vector<string> _desc_intoxicate_chance(const monster_info& mi,
                                              targeter* hitfunc, int pow)
{
    if (hitfunc && !hitfunc->affects_monster(mi))
        return vector<string>{"not susceptible"};

    int conf_pct = 40 + pow / 3;

    if (get_resist(mi.resists(), MR_RES_POISON) >= 1)
        conf_pct =  conf_pct / 3;

    return vector<string>{make_stringf("chance to confuse: %d%%", conf_pct)};
}

static vector<string> _desc_englaciate_chance(const monster_info& mi,
                                              targeter* hitfunc, int pow)
{
    if (hitfunc && !hitfunc->affects_monster(mi))
        return vector<string>{"not susceptible"};

    const int outcomes = pow * pow * pow;
    const int target   = 3 * mi.hd - 2;
    int fail_pct;

    // Tetrahedral number calculation to find the chance
    // 3 d pow < 3 * mi . hd + 1
    if (target <= pow)
        fail_pct = 100 * _tetrahedral_number(target) / outcomes;
    else if (target <= 2 * pow)
    {
        fail_pct = 100 * (_tetrahedral_number(target)
                       - 3 * _tetrahedral_number(target - pow)) / outcomes;
    }
    else if (target <= 3 * pow)
    {
        fail_pct = 100 * (outcomes
                       - _tetrahedral_number(3 * pow - target)) / outcomes;
    }
    else
        fail_pct = 100;

    return vector<string>{make_stringf("chance to slow: %d%%", 100 - fail_pct)};
}

static vector<string> _desc_gloom_chance(const monster_info& mi, int pow)
{
    if (mons_res_blind(mi.type))
        return vector<string>{"not susceptible"};

    return vector<string>{make_stringf("chance to dazzle: %d%%", gloom_success_chance(pow, mi.hd))};
}

static vector<string> _desc_airstrike_bonus(const monster_info& mi)
{
    const int empty_spaces = airstrike_space_around(mi.pos, false);
    return vector<string>{make_stringf("empty space bonus: %d/8", empty_spaces)};
}

static vector<string> _desc_mercury_weak_chance(const monster_info& mi)
{
    if (mi.is(MB_NO_ATTACKS))
        return vector<string>{};

    return vector<string>{make_stringf("chance to weaken: %d%%",
                            get_mercury_weaken_chance(mi.hd))};
}

static vector<string> _desc_warp_space_chance(int pow)
{
    return vector<string>{make_stringf("chance to blink: %d%%",
                            get_warp_space_chance(pow))};
}

static vector<string> _desc_meph_chance(const monster_info& mi)
{
    if (get_resist(mi.resists(), MR_RES_POISON) >= 1 || mi.is(MB_CLARITY))
        return vector<string>{"not susceptible"};

    int pct_chance = 2;
    if (mi.hd < MEPH_HD_CAP)
        pct_chance = 100 - (100 * mi.hd / MEPH_HD_CAP);
    return vector<string>{make_stringf("chance to affect: %d%%", pct_chance)};
}

static vector<string> _desc_hailstorm_hit_chance(const monster_info& mi, int pow)
{
    bolt beam;
    zappy(ZAP_HAILSTORM, pow, false, beam);
    return _desc_hit_chance(mi, beam.hit);
}

static vector<string> _desc_momentum_strike_hit_chance(const monster_info& mi, int pow)
{
    bolt beam;
    zappy(ZAP_MOMENTUM_STRIKE, pow, false, beam);
    return _desc_hit_chance(mi, beam.hit);
}

static vector<string> _desc_electric_charge_hit_chance(const monster_info& mi)
{
    melee_attack attk(&you, nullptr);
    attk.charge_pow = 1; // to give the accuracy bonus

    vector<string> desc;
    ostringstream result;
    // Pass in our own attack and specify distance 1 so we get correct to-hit
    // for where the attack *will* be from
    describe_to_hit(mi, result, nullptr, false, &attk, 1);
    desc.emplace_back(result.str());
    return desc;
}

static vector<string> _desc_insubstantial(const monster_info& mi, string desc)
{
    if (mons_class_flag(mi.type, M_INSUBSTANTIAL))
        return vector<string>{desc};

    return vector<string>{};
}

static vector<string> _desc_vampiric_draining_valid(const monster_info& mi)
{
    if (mi.mb.get(MB_CANT_DRAIN))
        return vector<string>{"not susceptible"};

    return vector<string>{};
}

static vector<string> _desc_rimeblight_valid(const monster_info& mi)
{
    if (mi.is(MB_RIMEBLIGHT))
        return vector<string>{"already infected"};
    else if (mons_class_is_peripheral(mi.type))
        return vector<string>{"not susceptible"};

    return vector<string>{};
}

static vector<string> _desc_dispersal_chance(const monster_info& mi, int pow)
{
    const int wl = mi.willpower();
    if (mons_class_is_stationary(mi.type))
        return vector<string>{"stationary"};

    if (wl == WILL_INVULN)
        return vector<string>{"will blink"};

    const int success = hex_success_chance(wl, pow, 100);
    return vector<string>{make_stringf("chance to teleport: %d%%", success)};
}

static vector<string> _desc_enfeeble_chance(const monster_info& mi, int pow)
{
    vector<string> base_effects;
    vector<string> all_effects;
    const int wl = mi.willpower();

    if (!mi.is(MB_NO_ATTACKS))
        base_effects.push_back("inflict weakness");
    if (mi.antimagic_susceptible())
        base_effects.push_back("diminish spells");
    if (!base_effects.empty())
    {
        all_effects.push_back("will " +
            comma_separated_line(base_effects.begin(), base_effects.end()));
    }
    if (wl != WILL_INVULN)
    {
        const int success = hex_success_chance(wl, pow, 100);
        all_effects.push_back(make_stringf("chance to daze and blind: %d%%", success));
    }

    if (all_effects.empty())
        return vector<string>{"not susceptible"};

    return all_effects;
}

static string _mon_threat_string(const CrawlStoreValue &mon_store)
{
    monster dummy;
    dummy.type = static_cast<monster_type>(mon_store.get_int());
    define_monster(dummy);

    int col;
    string desc;
    monster_info(&dummy).to_string(1, desc, col, true, nullptr, false);

    // Ghost demons need their underlying monster name. Without this,
    // we'll get e.g. a specific ugly thing colour based on what the
    // dummy monster rolled, which may not match what the actual monster
    // rolls.
    if (mons_is_ghost_demon(dummy.type))
        desc = get_monster_data(dummy.type)->name;

    const string col_name = colour_to_str(col);

    return "<" + col_name + ">" + article_a(desc) + "</" + col_name + ">";
}

// Include success chance in targeter for spells checking monster WL.
vector<string> desc_wl_success_chance(const monster_info& mi, int pow,
                                      targeter* hitfunc)
{
    targeter_beam* beam_hitf = dynamic_cast<targeter_beam*>(hitfunc);
    int wl = mi.willpower();
    if (wl == WILL_INVULN)
        return vector<string>{"infinite will"};
    if (hitfunc && !hitfunc->affects_monster(mi))
        return vector<string>{"not susceptible"};
    wl = apply_willpower_bypass(you, wl);
    vector<string> descs;
    if (beam_hitf && beam_hitf->beam.flavour == BEAM_POLYMORPH)
    {
        // Polymorph has a special effect on ugly things and shapeshifters that
        // does not require passing an WL check.
        if (mi.type == MONS_UGLY_THING || mi.type == MONS_VERY_UGLY_THING)
            return vector<string>{"will change colour"};
        if (mi.is(MB_SHAPESHIFTER))
            return vector<string>{"will change shape"};
        if (mi.type == MONS_SLIME_CREATURE && mi.slime_size > 1)
            descs.push_back("will probably split");

        // list out the normal poly set
        if (!mi.props.exists(POLY_SET_KEY))
            return vector<string>{"not susceptible"};
        const CrawlVector &set = mi.props[POLY_SET_KEY].get_vector();
        if (set.size() <= 0)
            return vector<string>{"not susceptible"};
        descs.push_back("will become "
                        + comma_separated_fn(set.begin(), set.end(),
                                             _mon_threat_string, ", or "));
    }

    const int success = hex_success_chance(wl, pow, 100);
    descs.push_back(make_stringf("chance to affect: %d%%", success));

    return descs;
}

class spell_targeting_behaviour : public targeting_behaviour
{
public:
    spell_targeting_behaviour(spell_type _spell)
        : targeting_behaviour(false), spell(_spell),
          err(spell_uselessness_reason(spell, true, false, true))
    {
    }

    bool targeted() override
    {
        return !!(get_spell_flags(spell) & spflag::targeting_mask);
    }

    string get_error() override
    {
        return err;
    }

    // TODO: provide useful errors for specific targets via get_monster_desc?

private:
    spell_type spell;
    string err;
};

// TODO: is there a way for this to be part of targeter objects, or
// direction_chooser itself?
desc_filter targeter_addl_desc(spell_type spell, int powc, spell_flags flags,
                                       targeter *hitfunc)
{
    // Add success chance to targeted spells checking monster WL
    const bool wl_check = testbits(flags, spflag::WL_check)
                          && !testbits(flags, spflag::helpful);
    if (wl_check && spell != SPELL_DISPERSAL)
    {
        const zap_type zap = spell_to_zap(spell);
        const int eff_pow = zap != NUM_ZAPS ? zap_ench_power(zap, powc,
                                                             false)
                                            :
              //XXX: deduplicate this with mass_enchantment?
              testbits(flags, spflag::area) ? min(200, ( powc * 3 ) / 2)
                                            : powc;

        if (spell == SPELL_ENFEEBLE)
            return bind(_desc_enfeeble_chance, placeholders::_1, eff_pow);
        else
            return bind(desc_wl_success_chance, placeholders::_1, eff_pow,
                        hitfunc);
    }
    switch (spell)
    {
        case SPELL_INTOXICATE:
            return bind(_desc_intoxicate_chance, placeholders::_1,
                        hitfunc, powc);
        case SPELL_ENGLACIATION:
            return bind(_desc_englaciate_chance, placeholders::_1,
                        hitfunc, powc);
        case SPELL_GLOOM:
            return bind(_desc_gloom_chance, placeholders::_1, powc);
        case SPELL_MEPHITIC_CLOUD:
        case SPELL_NOXIOUS_BREATH:
            return bind(_desc_meph_chance, placeholders::_1);
        case SPELL_VAMPIRIC_DRAINING:
            return bind(_desc_vampiric_draining_valid, placeholders::_1);
        case SPELL_RIMEBLIGHT:
            return bind(_desc_rimeblight_valid, placeholders::_1);
        case SPELL_STARBURST:
        {
            targeter_starburst* burst_hitf =
                dynamic_cast<targeter_starburst*>(hitfunc);
            if (!burst_hitf)
                break;
            targeter_starburst_beam* beam_hitf = &burst_hitf->beams[0];
            return bind(desc_beam_hit_chance, placeholders::_1, beam_hitf);
        }
        case SPELL_DISPERSAL:
            return bind(_desc_dispersal_chance, placeholders::_1, powc);
        case SPELL_AIRSTRIKE:
            return bind(_desc_airstrike_bonus, placeholders::_1);
        case SPELL_HAILSTORM:
            return bind(_desc_hailstorm_hit_chance, placeholders::_1, powc);
        case SPELL_MOMENTUM_STRIKE:
            return bind(_desc_momentum_strike_hit_chance, placeholders::_1, powc);
        case SPELL_ELECTRIC_CHARGE:
            return bind(_desc_electric_charge_hit_chance, placeholders::_1);
        case SPELL_FASTROOT:
            return bind(_desc_insubstantial, placeholders::_1, "immune to roots");
        case SPELL_STICKY_FLAME:
            return bind(_desc_insubstantial, placeholders::_1, "unstickable");
        case SPELL_PLASMA_BEAM:
            return bind(_desc_plasma_hit_chance, placeholders::_1, powc);
        case SPELL_MERCURY_ARROW:
            return _desc_mercury_weak_chance;
        case SPELL_WARP_SPACE:
            return bind(_desc_warp_space_chance, powc);
        default:
            break;
    }
    targeter_beam* beam_hitf = dynamic_cast<targeter_beam*>(hitfunc);
    if (beam_hitf && beam_hitf->beam.hit > 0 && !beam_hitf->beam.is_explosion)
        return bind(desc_beam_hit_chance, placeholders::_1, hitfunc);
    return nullptr;
}

/**
 * For the clua api, return the description displayed if targeting a monster
 * with a spell.
 *
 * @param mi     The targeted monster.
 * @param spell  The spell being cast.
 * @return       The displayed string.
 **/
string target_spell_desc(const monster_info& mi, spell_type spell)
{
    int powc = calc_spell_power(spell);
    const int range = calc_spell_range(spell, powc, false);

    unique_ptr<targeter> hitfunc = find_spell_targeter(spell, powc, range);
    if (!hitfunc)
        return "";

    desc_filter addl_desc = targeter_addl_desc(spell, powc,
                                get_spell_flags(spell), hitfunc.get());
    if (!addl_desc)
        return "";

    vector<string> d = addl_desc(mi);
    return comma_separated_line(d.begin(), d.end());
}

/**
 * Targets and fires player-cast spells & spell-like effects.
 *
 * Not all of these are actually real spells; invocations, decks or misc.
 * effects might also land us here.
 * Others are currently unused or unimplemented.
 *
 * @param spell         The type of spell being cast.
 * @param powc          Spellpower.
 * @param actual_spell  true if it is a spell being cast normally.
 *                      false if the spell is evoked or from an innate or
 *                      divine ability.
 * @param evoked_wand   The wand the spell was evoked from if applicable, or
 *                      nullptr.
 * @param force_failure True if the spell's failure has already been determined
 *                      in advance (for spells being cast via an innate or
 *                      divine ability).
 * @return spret::success if spell is successfully cast for purposes of
 * exercising, spret::fail otherwise, or spret::abort if the player cancelled
 * the casting.
 **/
spret your_spells(spell_type spell, int powc, bool actual_spell,
                  const item_def* const evoked_wand, dist *target,
                  bool force_failure)
{
    ASSERT(!crawl_state.game_is_arena());
    ASSERT(!(actual_spell && evoked_wand));
    ASSERT(!evoked_wand || evoked_wand->base_type == OBJ_WANDS);
    ASSERT(!force_failure || !actual_spell && !evoked_wand);

    const bool wiz_cast = (crawl_state.prev_cmd == CMD_WIZARD && !actual_spell);
    const bool can_enkindle = actual_spell && spell_can_be_enkindled(spell);
    const bool enkindled = can_enkindle && you.duration[DUR_ENKINDLED];

    dist target_local;
    if (!target)
        target = &target_local;
    bolt beam;
    beam.origin_spell = spell;

    // [dshaligram] Any action that depends on the spellcasting attempt to have
    // succeeded must be performed after the switch.
    if (!wiz_cast && _spellcasting_aborted(spell))
        return spret::abort;

    const spell_flags flags = get_spell_flags(spell);

    if (!powc)
        powc = calc_spell_power(spell);

    const int range = calc_spell_range(spell, powc, actual_spell);
    beam.range = range;

    unique_ptr<targeter> hitfunc = find_spell_targeter(spell, powc, range);
    const bool is_targeted = !!(flags & spflag::targeting_mask);

    const god_type god =
        (crawl_state.is_god_acting()) ? crawl_state.which_god_acting()
                                      : GOD_NO_GOD;

    // XXX: This handles only some of the cases where spells need
    // targeting. There are others that do their own that will be
    // missed by this (and thus will not properly ESC without cost
    // because of it). Hopefully, those will eventually be fixed. - bwr
    // TODO: what's the status of the above comment in 2020+?
    const bool use_targeter = is_targeted
        || !god // Don't allow targeting spells cast by Xom
           && hitfunc
           && (target->fire_context // force static targeters when called in
                                    // "fire" mode
               || Options.always_use_static_spell_targeters
               || Options.force_spell_targeter.count(spell) > 0);

    if (use_targeter)
    {
        const targ_mode_type targ =
              spell == SPELL_PLATINUM_PARAGON           ? TARG_HOSTILE_OR_EMPTY :
              testbits(flags, spflag::aim_at_space)     ? TARG_NON_ACTOR :
              testbits(flags, spflag::helpful)          ? TARG_FRIEND :
              testbits(flags, spflag::obj)              ? TARG_MOVABLE_OBJECT :
                                                          TARG_HOSTILE;

        // TODO: if any other spells ever need this, add an spflag
        // (right now otherwise used only on god abilities)
        const targeting_type dir =
            spell == SPELL_BLINKBOLT ? DIR_ENFORCE_RANGE
            : testbits(flags, spflag::target) ? DIR_ENFORCE_RANGE : DIR_NONE;

        // TODO: it's extremely inconsistent when this prompt shows up, not
        // sure why
        const char *prompt = get_spell_target_prompt(spell);

        const bool needs_path = !testbits(flags, spflag::target)
                                // Apportation must be spflag::target, since a
                                // shift-direction makes no sense for it, but
                                // it nevertheless requires line-of-fire.
                                || spell == SPELL_APPORTATION;

        desc_filter additional_desc
            = targeter_addl_desc(spell, powc, flags, hitfunc.get());

        // `true` on fourth param skips MP check and a few others that have
        // already been carried out
        const bool useless = spell_is_useless(spell, true, false, true);
        const char *spell_title_color = useless ? "darkgrey" : "w";
        const string verb = channelled_spell_active(spell)
            ? "<lightred>Restarting spell</lightred>"
            : is_targeted ? "Aiming" : "Casting";
        string title = make_stringf("%s: <%s>%s</%s>", verb.c_str(),
                    spell_title_color, spell_title(spell), spell_title_color);

        if (spell == SPELL_GRAVE_CLAW)
        {
            title += make_stringf("<lightgrey> (%d/%d uses available)</lightgrey>",
                                  you.props[GRAVE_CLAW_CHARGES_KEY].get_int(),
                                  GRAVE_CLAW_MAX_CHARGES);
        }

        spell_targeting_behaviour beh(spell);

        direction_chooser_args args;
        args.hitfunc = hitfunc.get();
        args.restricts = dir;
        args.mode = targ;
        args.range = range;
        args.needs_path = needs_path;
        args.target_prefix = prompt;
        args.top_prompt = title;
        args.behaviour = &beh;

        if (testbits(flags, spflag::prefer_farthest))
            args.prefer_farthest = true;

        if (spell == SPELL_SHOCK)
            args.try_multizap = true;

        // if the spell is useless and we have somehow gotten this far, it's
        // a forced cast. Setting this prevents the direction chooser from
        // looking for selecting a default target (which doesn't factor in
        // the spell's capabilities).
        // Also ensure we don't look for a target for static targeters. It might
        // be better to move to an affected position if any?
        if (useless || !is_targeted)
            args.default_place = you.pos();
        if (hitfunc && hitfunc->can_affect_walls())
        {
            args.show_floor_desc = true;
            args.show_boring_feats = false; // don't show "The floor."
        }
        if (testbits(flags, spflag::not_self))
            args.self = confirm_prompt_type::cancel;
        else
            args.self = confirm_prompt_type::none;
        args.get_desc_func = additional_desc;
        if (!spell_direction(*target, beam, &args))
            return spret::abort;

        if (testbits(flags, spflag::not_self) && target->isMe())
        {
            if (spell == SPELL_TELEPORT_OTHER)
                mpr("Sorry, this spell works on others only.");
            else
                canned_msg(MSG_UNTHINKING_ACT);

            return spret::abort;
        }

        if (spell == SPELL_BOMBARD)
        {
            const coord_def back = you.stumble_pos(target->target);
            if (!back.origin()
                && back != you.pos()
                && !check_moveto(back, "potentially stumble back", false))
            {
                return spret::abort;
            }
        }
    }

    if (evoked_wand)
        surge_power_wand(wand_mp_cost());
    else if (actual_spell)
        surge_power(_spell_enhancement(spell));

    int fail = 0;

    dprf("Spell #%d, power=%d", spell, powc);

    const coord_def orig_target_pos = beam.target;

    spret cast_result = _do_cast(spell, powc, *target, beam, god,
                                 force_failure || fail, actual_spell);

    switch (cast_result)
    {
    case spret::success:
    {
        _apply_post_zap_effect(spell, orig_target_pos);

        const int demonic_magic = you.get_mutation_level(MUT_DEMONIC_MAGIC);
        const bool ephemeral_shield = you.get_mutation_level(MUT_EPHEMERAL_SHIELD);

        if ((demonic_magic == 3 && evoked_wand)
            || (demonic_magic > 0 && (actual_spell || you.divine_exegesis)))
        {
            do_demonic_magic(spell_difficulty(spell) * 6, demonic_magic);
        }

        if (ephemeral_shield && (actual_spell || you.divine_exegesis))
        {
            you.set_duration(DUR_EPHEMERAL_SHIELD, 2);
            you.redraw_armour_class = true;
        }

        if (you.props.exists(BATTLESPHERE_KEY)
            && (actual_spell || you.divine_exegesis)
            && battlesphere_can_mirror(spell))
        {
            trigger_battlesphere(&you);
        }

        if (will_have_passive(passive_t::shadow_spells) && actual_spell)
            dithmenos_shadow_spell(spell);
        _spellcasting_side_effects(spell, god, !actual_spell);

        if (you.wearing_ego(OBJ_GIZMOS, SPGIZMO_SPELLMOTOR) && actual_spell)
            coglin_spellmotor_attack();

        // Handle revenant passives
        if (can_enkindle && you.has_mutation(MUT_SPELLCLAWS))
            spellclaws_attack(spell_difficulty(spell));

        if (enkindled && --you.props[ENKINDLE_CHARGES_KEY].get_int() == 0)
            end_enkindled_status();

        return spret::success;
    }
    case spret::fail:
    {
        if (actual_spell)
        {
            mprf("You miscast %s.", spell_title(spell));
            flush_input_buffer(FLUSH_ON_FAILURE);
            learned_something_new(HINT_SPELL_MISCAST);
            miscast_effect(spell, fail);
        }

        return spret::fail;
    }

    case spret::abort:
        return spret::abort;

    case spret::none:
#ifdef WIZARD
        if (you.wizard && !actual_spell && is_valid_spell(spell)
            && (flags & spflag::monster))
        {
            _try_monster_cast(spell, powc, *target, beam);
            return spret::success;
        }
#endif

        if (is_valid_spell(spell))
        {
            mprf(MSGCH_ERROR, "Spell '%s' is not a player castable spell.",
                 spell_title(spell));
        }
        else
            mprf(MSGCH_ERROR, "Invalid spell!");

        return spret::abort;
    }

    return spret::success;
}

// Returns spret::success, spret::abort, spret::fail
// or spret::none (not a player spell).
static spret _do_cast(spell_type spell, int powc, const dist& spd,
                           bolt& beam, god_type god, bool fail,
                           bool actual_spell)
{
    if (actual_spell && !you.wizard
        && (get_spell_flags(spell) & (spflag::monster | spflag::testing)))
    {
        return spret::none;
    }

    const coord_def target = spd.isTarget ? beam.target : you.pos() + spd.delta;
    if (spell == SPELL_FREEZE)
    {
        if (!adjacent(you.pos(), target))
            return spret::abort;
    }

    switch (spell)
    {
    case SPELL_FREEZE:
        return cast_freeze(powc, monster_at(target), fail);

    case SPELL_IOOD:
        return cast_iood(&you, powc, &beam, 0, 0, MHITNOT, fail);

    // Clouds and explosions.
    case SPELL_POISONOUS_CLOUD:
    case SPELL_HOLY_BREATH:
    case SPELL_FREEZING_CLOUD:
        return cast_big_c(powc, spell, &you, beam, fail);

    case SPELL_FIRE_STORM:
        return cast_fire_storm(powc, beam, fail);

    // Demonspawn ability, no failure.
    case SPELL_CALL_DOWN_DAMNATION:
        return cast_smitey_damnation(powc, beam) ? spret::success : spret::abort;

    // LOS spells
    case SPELL_SMITING:
        return cast_smiting(powc, monster_at(target), fail);

    case SPELL_AIRSTRIKE:
        return cast_airstrike(powc, spd.target, fail);

    case SPELL_MOMENTUM_STRIKE:
        return cast_momentum_strike(powc, spd.target, fail);

    case SPELL_LRD:
        return cast_fragmentation(powc, &you, spd.target, fail);

    case SPELL_GRAVITAS:
        return cast_gravitas(powc, beam.target, fail);

    case SPELL_VIOLENT_UNRAVELLING:
        return cast_unravelling(spd.target, powc, fail);

    // other effects
    case SPELL_DISCHARGE:
        return cast_discharge(powc, you, fail);

    case SPELL_ARCJOLT:
        return cast_arcjolt(powc, you, fail);

    case SPELL_PLASMA_BEAM:
        return cast_plasma_beam(powc, you, fail);

    case SPELL_CHAIN_LIGHTNING:
        return cast_chain_lightning(powc, you, fail);

    case SPELL_DISPERSAL:
        return cast_dispersal(powc, fail);

    case SPELL_SHATTER:
        return cast_shatter(powc, fail);

    case SPELL_SCORCH:
        return cast_scorch(you, powc, fail);

    case SPELL_POISONOUS_VAPOURS:
        return cast_poisonous_vapours(you, powc, target, fail);

    case SPELL_IRRADIATE:
        return cast_irradiate(powc, you, fail);

    case SPELL_LEDAS_LIQUEFACTION:
        return cast_liquefaction(powc, fail);

    case SPELL_OZOCUBUS_REFRIGERATION:
        return fire_los_attack_spell(spell, powc, &you, fail);

    case SPELL_OLGREBS_TOXIC_RADIANCE:
        return cast_toxic_radiance(&you, powc, fail);

    case SPELL_IGNITE_POISON:
        return cast_ignite_poison(&you, powc, fail);

    case SPELL_POLAR_VORTEX:
        return cast_polar_vortex(powc, fail);

    case SPELL_THUNDERBOLT:
        return cast_thunderbolt(&you, powc, target, fail);

    case SPELL_GLOOM:
        return cast_gloom(&you, powc, fail);

    case SPELL_CHAIN_OF_CHAOS:
        return cast_chain_spell(SPELL_CHAIN_OF_CHAOS, powc, &you, fail);

    case SPELL_IGNITION:
        return cast_ignition(&you, powc, fail);

    case SPELL_FROZEN_RAMPARTS:
        return cast_frozen_ramparts(powc, fail);

    // Summoning spells, and other spells that create new monsters.
    case SPELL_SUMMON_SMALL_MAMMAL:
        return cast_summon_small_mammal(powc, fail);

    case SPELL_CALL_CANINE_FAMILIAR:
        return cast_call_canine_familiar(powc, fail);

    case SPELL_AWAKEN_ARMOUR:
        return cast_awaken_armour(powc, fail);

    case SPELL_SUMMON_ICE_BEAST:
        return cast_summon_ice_beast(powc, fail);

    case SPELL_SUMMON_CACTUS:
        return cast_summon_cactus(powc, fail);

    case SPELL_SPHINX_SISTERS:
        return cast_sphinx_sisters(you, powc, fail);;

    case SPELL_SUMMON_DRAGON:
        return cast_summon_dragon(&you, powc, fail);

    case SPELL_DRAGON_CALL:
        return cast_dragon_call(powc, fail);

    case SPELL_SUMMON_HYDRA:
        return cast_summon_hydra(&you, powc, fail);

    case SPELL_SUMMON_MANA_VIPER:
        return cast_summon_mana_viper(powc, fail);

    case SPELL_SUMMON_SEISMOSAURUS_EGG:
        return cast_summon_seismosaurus_egg(you, powc, fail);

    case SPELL_CONJURE_BALL_LIGHTNING:
        return cast_conjure_ball_lightning(powc, fail);

    case SPELL_FORGE_LIGHTNING_SPIRE:
        return cast_forge_lightning_spire(powc, fail);

    case SPELL_FORGE_BLAZEHEART_GOLEM:
        return cast_forge_blazeheart_golem(powc, fail);

    case SPELL_CALL_IMP:
        return cast_call_imp(powc, fail);

    case SPELL_SUMMON_HORRIBLE_THINGS:
        return cast_summon_horrible_things(powc, fail);

    case SPELL_MALIGN_GATEWAY:
        return cast_malign_gateway(&you, powc, fail);

    case SPELL_SUMMON_FOREST:
        return cast_summon_forest(&you, powc, fail);

    case SPELL_ANIMATE_DEAD:
        return cast_animate_dead(powc, fail);

    case SPELL_MARTYRS_KNELL:
        return cast_martyrs_knell(&you, powc, fail);

    case SPELL_HAUNT:
        return cast_haunt(powc, beam.target, fail);

    case SPELL_DEATH_CHANNEL:
        return cast_death_channel(powc, god, fail);

    case SPELL_SPELLSPARK_SERVITOR:
        return cast_spellspark_servitor(powc, fail);

    case SPELL_BATTLESPHERE:
        return cast_battlesphere(&you, powc, fail);

    case SPELL_INFESTATION:
        return cast_infestation(powc, beam, fail);

    case SPELL_FOXFIRE:
        return cast_foxfire(you, powc, fail);

    case SPELL_NOXIOUS_BOG:
        return cast_noxious_bog(powc, fail);

    case SPELL_CLOCKWORK_BEE:
        return cast_clockwork_bee(beam.target, fail);

    case SPELL_SPIKE_LAUNCHER:
        return cast_spike_launcher(powc, fail);

    case SPELL_DIAMOND_SAWBLADES:
        return cast_diamond_sawblades(powc, fail);

    case SPELL_SURPRISING_CROCODILE:
        return cast_surprising_crocodile(you, beam.target, powc, fail);

    case SPELL_PLATINUM_PARAGON:
        return cast_platinum_paragon(beam.target, powc, fail);

    case SPELL_WALKING_ALEMBIC:
        return cast_walking_alembic(you, powc, fail);

    case SPELL_MONARCH_BOMB:
        return cast_monarch_bomb(you, powc, fail);

    case SPELL_SPLINTERFROST_SHELL:
        return cast_splinterfrost_shell(you, beam.target, powc, fail);

    case SPELL_PERCUSSIVE_TEMPERING:
        return cast_percussive_tempering(you, *monster_at(beam.target), powc, fail);

    case SPELL_FORTRESS_BLAST:
        return cast_fortress_blast(you, powc, fail);

    case SPELL_PHALANX_BEETLE:
        return cast_phalanx_beetle(you, powc, fail);

    case SPELL_RENDING_BLADE:
        return cast_rending_blade(powc, fail);

    // Enchantments.
    case SPELL_CONFUSING_TOUCH:
        return cast_confusing_touch(powc, fail);

    case SPELL_CAUSE_FEAR:
        return mass_enchantment(ENCH_FEAR, powc, fail);

    case SPELL_ANGUISH:
        return mass_enchantment(ENCH_ANGUISH, powc, fail);

    case SPELL_INTOXICATE:
        return cast_intoxicate(powc, fail);

    case SPELL_DISCORD:
        return mass_enchantment(ENCH_FRENZIED, powc, fail);

    case SPELL_ENGLACIATION:
        return cast_englaciation(powc, fail);

    case SPELL_BORGNJORS_VILE_CLUTCH:
        return cast_vile_clutch(powc, beam, fail);

    case SPELL_PUTREFACTION:
        return cast_putrefaction(monster_at(target), powc, fail);

    // Our few remaining self-enchantments.
    case SPELL_SWIFTNESS:
        return cast_swiftness(powc, fail);

    case SPELL_OZOCUBUS_ARMOUR:
        return ice_armour(powc, fail);

    case SPELL_SILENCE:
        return cast_silence(powc, fail);

    case SPELL_FUGUE_OF_THE_FALLEN:
        return cast_fugue_of_the_fallen(powc, fail);

    case SPELL_DIMENSIONAL_BULLSEYE:
        return cast_dimensional_bullseye(powc, monster_at(target), fail);

    case SPELL_DETONATION_CATALYST:
        return cast_detonation_catalyst(fail);

    // other
    case SPELL_BORGNJORS_REVIVIFICATION:
        return cast_revivification(powc, fail);

    case SPELL_SUBLIMATION_OF_BLOOD:
        return cast_sublimation_of_blood(powc, fail);

    case SPELL_DEATHS_DOOR:
        return cast_deaths_door(powc, fail);

    // Escape spells.
    case SPELL_BLINK:
        return cast_blink(powc, fail);

    case SPELL_BLASTMOTE:
        return kindle_blastmotes(powc, fail);

    case SPELL_PASSWALL:
        return cast_passwall(beam.target, powc, fail);

    case SPELL_APPORTATION:
        return cast_apportation(powc, beam, fail);

    case SPELL_DISJUNCTION:
        return cast_disjunction(powc, fail);

    case SPELL_MANIFOLD_ASSAULT:
        return cast_manifold_assault(you, powc, fail);

    case SPELL_GOLUBRIAS_PASSAGE:
        return cast_golubrias_passage(powc, beam.target, fail);

    case SPELL_FULMINANT_PRISM:
        return cast_fulminating_prism(&you, powc, beam.target, fail);

    case SPELL_SEARING_RAY:
        return cast_searing_ray(you, powc, beam, fail);

    case SPELL_FLAME_WAVE:
        return cast_flame_wave(powc, fail);

    case SPELL_INNER_FLAME:
        return cast_inner_flame(spd.target, powc, fail);

    case SPELL_TELEPORT_OTHER:
        return cast_teleport_other(spd.target, powc, fail);

    case SPELL_SIMULACRUM:
        return cast_simulacrum(spd.target, powc, fail);

    case SPELL_GLACIATE:
        return cast_glaciate(&you, powc, target, fail);

    case SPELL_GRAVE_CLAW:
        return cast_grave_claw(you, spd.target, powc, fail);

    case SPELL_BLINKBOLT:
        return blinkbolt(powc, beam, fail);

    case SPELL_ELECTRIC_CHARGE:
        return electric_charge(you, powc, fail, beam.target);

    case SPELL_STARBURST:
        return cast_starburst(powc, fail);

    case SPELL_HAILSTORM:
        return cast_hailstorm(powc, fail);

    case SPELL_MAXWELLS_COUPLING:
        return cast_maxwells_coupling(powc, fail);

    case SPELL_ISKENDERUNS_MYSTIC_BLAST:
        return cast_imb(powc, fail);

    case SPELL_JINXBITE:
        return cast_jinxbite(powc, fail);

    case SPELL_SIGIL_OF_BINDING:
        return cast_sigil_of_binding(powc, fail, false);

    case SPELL_BOULDER:
        return cast_broms_barrelling_boulder(you, beam.target, powc, fail);

    case SPELL_PERMAFROST_ERUPTION:
        return cast_permafrost_eruption(you, powc, fail);

    case SPELL_PILEDRIVER:
        return cast_piledriver(beam.target, powc, fail);

    // Just to do extra messaging; spell is handled by default zapping
    case SPELL_COMBUSTION_BREATH:
    case SPELL_GLACIAL_BREATH:
    case SPELL_STEAM_BREATH:
    case SPELL_CAUSTIC_BREATH:
    case SPELL_MUD_BREATH:
    case SPELL_NULLIFYING_BREATH:
    case SPELL_NOXIOUS_BREATH:
    {
        static map<spell_type, string> breath_message =
        {
            { SPELL_COMBUSTION_BREATH, "You breathe a blast of explosive embers." },
            { SPELL_GLACIAL_BREATH, "You exhale a wave of glacial cold." },
            { SPELL_STEAM_BREATH, "You exhale a blast of scalding steam." },
            { SPELL_NULLIFYING_BREATH, "You breathe a sphere of nullifying energy." },
            { SPELL_NOXIOUS_BREATH, "You exhale a blast of noxious fumes." },
            { SPELL_CAUSTIC_BREATH, "You breathe a spray of caustic vapour." },
            { SPELL_MUD_BREATH, "You spew a torrent of mud." },
            { SPELL_GALVANIC_BREATH, "You breathe wild lightning."}
        };

        if (!fail)
            mpr(breath_message[spell].c_str());
    }
    break;

    case SPELL_GOLDEN_BREATH:
        return cast_golden_breath(beam, powc, fail);

    case SPELL_GELLS_GAVOTTE:
        return cast_gavotte(powc, beam.target - you.pos(), fail);

    case SPELL_MAGNAVOLT:
        return cast_magnavolt(beam.target, powc, fail);

    case SPELL_FULSOME_FUSILLADE:
        return cast_fulsome_fusillade(powc, fail);

    case SPELL_HOARFROST_CANNONADE:
        return cast_hoarfrost_cannonade(you, powc, fail);

    case SPELL_HELLFIRE_MORTAR:
        return cast_hellfire_mortar(you, beam, powc, fail);

    default:
        if (spell_removed(spell))
        {
            mprf("Sorry, the spell '%s' is gone!", spell_title(spell));
            return spret::abort;
        }
        break;
    }

    // Finally, try zaps.
    zap_type zap = spell_to_zap(spell);
    if (zap != NUM_ZAPS)
        return zapping(zap, powc,  beam, true, nullptr, fail);

    return spret::none;
}

string spell_noise_string(spell_type spell, int chop_wiz_display_width)
{
    const int casting_noise = spell_noise(spell);
    int effect_noise = spell_effect_noise(spell);

    // A typical amount of noise.
    if (spell == SPELL_POLAR_VORTEX)
        effect_noise = 15;

    const int noise = max(casting_noise, effect_noise);

    const char* noise_descriptions[] =
    {
        "Silent", "Almost silent", "Quiet", "A bit loud", "Loud", "Very loud",
        "Extremely loud", "Deafening"
    };

    const int breakpoints[] = { 1, 2, 4, 8, 15, 20, 30 };
    COMPILE_CHECK(ARRAYSZ(noise_descriptions) == 1 + ARRAYSZ(breakpoints));

    const char* desc = noise_descriptions[breakpoint_rank(noise, breakpoints,
                                                ARRAYSZ(breakpoints))];

#ifdef WIZARD
    if (you.wizard)
    {
        if (chop_wiz_display_width > 0)
        {
            ostringstream shortdesc;
            shortdesc << chop_string(desc, chop_wiz_display_width)
                      << "(" << to_string(noise) << ")";
            return shortdesc.str();
        }
        else
            return make_stringf("%s (%d)", desc, noise);
    }
    else
#endif
        return desc;
}

#ifdef WIZARD
static string _wizard_spell_power_numeric_string(spell_type spell)
{
    const int cap = spell_power_cap(spell);
    if (cap == 0)
        return "N/A";
    const int power = min(calc_spell_power(spell), cap);
    return make_stringf("%d (%d)", power, cap);
}
#endif

// TODO: deduplicate with the same-named function in describe-spells.cc
static dice_def _spell_damage(spell_type spell, int power)
{
    if (power < 0)
        return dice_def(0,0);
    switch (spell)
    {
        case SPELL_FULMINANT_PRISM:
            return prism_damage(prism_hd(power, false), true);
        case SPELL_CONJURE_BALL_LIGHTNING:
            return ball_lightning_damage(ball_lightning_hd(power, false), false);
        case SPELL_IOOD:
            return iood_damage(power, INFINITE_DISTANCE, false);
        case SPELL_IRRADIATE:
            return irradiate_damage(power, false);
        case SPELL_SHATTER:
            return shatter_damage(power);
        case SPELL_SCORCH:
            return scorch_damage(power, false);
        case SPELL_BATTLESPHERE:
            return battlesphere_damage_from_power(power);
        case SPELL_FROZEN_RAMPARTS:
            return ramparts_damage(power, false);
        case SPELL_LRD:
            return base_fragmentation_damage(power, false);
        case SPELL_ARCJOLT:
            return arcjolt_damage(power, false);
        case SPELL_POLAR_VORTEX:
            return polar_vortex_dice(power, false);
        case SPELL_NOXIOUS_BOG:
            return toxic_bog_damage();
        case SPELL_BOULDER:
            return boulder_damage(power, false);
        case SPELL_THUNDERBOLT:
            return thunderbolt_damage(power, 1);
        case SPELL_HELLFIRE_MORTAR:
            return hellfire_mortar_damage(power);
        case SPELL_FORGE_LIGHTNING_SPIRE:
            return lightning_spire_damage(power);
        case SPELL_DIAMOND_SAWBLADES:
            return diamond_sawblade_damage(power);
        case SPELL_FORTRESS_BLAST:
            return fortress_blast_damage(you.armour_class_scaled(1), false);
        case SPELL_POISONOUS_VAPOURS:
            return poisonous_vapours_damage(power, false);
        case SPELL_DETONATION_CATALYST:
            return detonation_catalyst_damage(power, false);
        case SPELL_JINXBITE:
            return jinxbite_damage(power,false);
        default:
            break;
    }
    const zap_type zap = spell_to_zap(spell);
    if (zap == NUM_ZAPS)
        return dice_def(0,0);
    return zap_damage(zap, power, false, false);
}

string spell_max_damage_string(spell_type spell)
{
    switch (spell)
    {
    case SPELL_MAXWELLS_COUPLING:
    case SPELL_FREEZING_CLOUD:
        // These have damage strings, but don't scale with power.
        return "";
    case SPELL_FORTRESS_BLAST:
    {
        // Fortress Blast's damage scales with AC, not spellpower, and thus
        // exceeds the normal spellpower cap.
        dice_def dmg = zap_damage(ZAP_FORTRESS_BLAST, 200, false);
        return make_stringf("%dd%d", dmg.num, dmg.size);
    }
    default:
        break;
    }
    // Only show a distinct max damage string if we're not at max power
    // already. Otherwise, it's redundant!
    const int pow = calc_spell_power(spell);
    const int max_pow = spell_power_cap(spell);
    if (pow >= max_pow)
        return "";
    return spell_damage_string(spell, false, max_pow);
}

string spell_damage_string(spell_type spell, bool evoked, int pow, bool terse)
{
    if (pow == -1)
        pow = evoked ? wand_power(spell) : calc_spell_power(spell);
    switch (spell)
    {
        case SPELL_MAXWELLS_COUPLING:
            return Options.char_set == CSET_ASCII ? "death" : "\u221e"; //"∞"
        case SPELL_FREEZING_CLOUD:
            return desc_cloud_damage(CLOUD_COLD, false);
        case SPELL_DISCHARGE:
        {
            const int max = discharge_max_damage(pow);
            return make_stringf("%d-%d/arc", FLAT_DISCHARGE_ARC_DAMAGE, max);
        }
        case SPELL_AIRSTRIKE:
            return describe_airstrike_dam(base_airstrike_damage(pow));
        case SPELL_PILEDRIVER:
            return make_stringf("(3-9)d%d", piledriver_collision_damage(pow, 1, false).size);
        case SPELL_GELLS_GAVOTTE:
            return make_stringf("2d(%d-%d)", gavotte_impact_damage(pow, 1, false).size,
                                             gavotte_impact_damage(pow, 4, false).size);

        case SPELL_FULSOME_FUSILLADE:
            return make_stringf("(3-5)d%d", _spell_damage(spell, pow).size);
        case SPELL_RIMEBLIGHT:
            return describe_rimeblight_damage(pow, terse);
        case SPELL_HOARFROST_CANNONADE:
        {
            dice_def shot_dam = hoarfrost_cannonade_damage(pow, false);
            dice_def finale_dam = hoarfrost_cannonade_damage(pow, true);

            return make_stringf("%dd%d/%dd%d",
                shot_dam.num, shot_dam.size, finale_dam.num, finale_dam.size);
        }
        case SPELL_RENDING_BLADE:
        {
            dice_def dmg_mp = rending_blade_damage(pow, true);
            dice_def dmg = rending_blade_damage(pow, false);
            const int bonus = dmg_mp.size - dmg.size;
            if (bonus > 0)
                return make_stringf("%dd(%d+%d*)", dmg.num, dmg.size, bonus);
            else
                return make_stringf("%dd%d", dmg.num, dmg.size);
        }
        default:
            break;
    }
    const dice_def dam = _spell_damage(spell, pow);
    if (dam.num == 0 || dam.size == 0)
        return "";
    string mult = "";
    switch (spell)
    {
        case SPELL_FOXFIRE:
        case SPELL_PLASMA_BEAM:
        case SPELL_PERMAFROST_ERUPTION:
            mult = "2x";
            break;
        case SPELL_CONJURE_BALL_LIGHTNING:
            mult = "3x";
            break;
        case SPELL_TREMORSTONE:
            mult = make_stringf("%dx", tremorstone_count(pow));
        default:
            break;
    }
    const string dam_str = make_stringf("%s%dd%d", mult.c_str(), dam.num,
            dam.size);

    if (spell == SPELL_ISKENDERUNS_MYSTIC_BLAST)
    {
        if (terse)
            return dam_str + "+";
        else
            return make_stringf("%s (+%s)",
                dam_str.c_str(),
                describe_collision_dam(default_collision_damage(pow, false)).c_str());
    }

    if (spell == SPELL_LRD
        || spell == SPELL_SHATTER
        || spell == SPELL_POLAR_VORTEX)
    {
        return dam_str + "*"; // many special cases of more/less damage
    }
    return dam_str;
}

int spell_acc(spell_type spell)
{
    const zap_type zap = spell_to_zap(spell);
    if (zap == NUM_ZAPS)
        return -1;
    if (zap_explodes(zap) || zap_is_enchantment(zap))
        return -1;
    const int power = calc_spell_power(spell);
    if (power < 0)
        return -1;
    const int acc = zap_to_hit(zap, power, false);
    if (acc == AUTOMATIC_HIT)
        return -1;
    return acc;
}

int spell_power_percent(spell_type spell)
{
    const int pow = calc_spell_power(spell);
    const int max_pow = spell_power_cap(spell);
    if (max_pow == 0)
        return -1; // should never happen for player spells
    return pow * 100 / max_pow;
}

string spell_power_string(spell_type spell)
{
    return _wizard_spell_power_numeric_string(spell);
}

int calc_spell_range(spell_type spell, int power, bool allow_bonus,
                     bool ignore_shadows)
{
    if (power == 0)
        power = calc_spell_power(spell);
    const int range = spell_range(spell, power, allow_bonus, ignore_shadows);

    return range;
}

/**
 * Give a string describing a given spell's range, as cast by the player.
 *
 * @param spell     The spell in question.
 * @return          See above.
 */
string spell_range_string(spell_type spell)
{
    const int range    = calc_spell_range(spell, 0);
    const int maxrange = spell_has_variable_range(spell)
                            ? calc_spell_range(spell, spell_power_cap(spell), true, true)
                            : -1;

    return range_string(range, maxrange, spell == SPELL_HAILSTORM ? 2 : 0);
}

/**
 * Give a string describing a given spell's range.
 *
 * For spells with variable range, will be written in the form of 'X/Y' where X
 * is the current range and Y is the maximum range.
 *
 * For spells with a minimum range (ie: Call Down Lightning), will be written in
 * the form of 'X-Y' where X is the minimal effective range.
 *
 * @param range         The current range of the spell.
 * @param maxrange      The range the spell would have at max power.
 *                      -1 if the spell does not have variable range.
 * @param minrange      The minimal range at which the spell is castable.
 * @return              See above.
 */
string range_string(int range, int maxrange, int minrange)
{
    if (range <= 0)
        return "N/A";

    string ret = to_string(range);
    if (maxrange > -1)
        ret += "/" + to_string(maxrange);
    if (minrange > 0)
        ret = to_string(minrange) + "-" + ret;

    return ret;
}

string spell_schools_string(spell_type spell)
{
    string desc;

    bool already = false;
    for (const auto bit : spschools_type::range())
    {
        if (spell_typematch(spell, bit))
        {
            if (already)
                desc += "/";
            desc += spelltype_long_name(bit);
            already = true;
        }
    }

    return desc;
}

void spell_skills(spell_type spell, set<skill_type> &skills)
{
    const spschools_type disciplines = get_spell_disciplines(spell);
    for (const auto bit : spschools_type::range())
        if (disciplines & bit)
            skills.insert(spell_type2skill(bit));
}

void do_demonic_magic(int pow, int rank)
{
    if (rank < 1)
        return;

    mpr("Malevolent energies surge around you.");

    for (radius_iterator ri(you.pos(), rank, C_SQUARE, LOS_NO_TRANS, true); ri; ++ri)
    {
        monster *mons = monster_at(*ri);

        if (!mons || mons->wont_attack() || !mons_is_threatening(*mons))
            continue;

        if (mons->check_willpower(&you, pow) <= 0)
            mons->paralyse(&you, random_range(2, 5));
    }
}

bool channelled_spell_active(spell_type spell)
{
    return you.attribute[ATTR_CHANNELLED_SPELL] == spell;
}

void start_channelling_spell(spell_type spell, string reminder_msg, bool do_effect)
{
    you.attribute[ATTR_CHANNELLED_SPELL] = spell;
    you.attribute[ATTR_CHANNEL_DURATION] = -1;

    if (do_effect)
        handle_channelled_spell();
    else
        you.attribute[ATTR_CHANNEL_DURATION] = 0;

    if (!reminder_msg.empty())
    {
        string msg = "(Press <w>%</w> to " + reminder_msg + ".)";
        insert_commands(msg, { CMD_WAIT });
        mpr(msg);
    }
}

void handle_channelled_spell()
{
    if (you.attribute[ATTR_CHANNELLED_SPELL] == SPELL_NO_SPELL)
        return;

    // Skip processing at the end of the turn this is cast (since that already
    // happened *as* it was cast and shouldn't happen a second time.)
    if (++you.attribute[ATTR_CHANNEL_DURATION] == 1)
        return;

    const spell_type spell = (spell_type)you.attribute[ATTR_CHANNELLED_SPELL];

    // This value is -1 at the moment the spell is started. 0 is 'skipped' as
    // the end of the turn it is started. 1+ represents subsequent turns.
    const int turn = (you.attribute[ATTR_CHANNEL_DURATION] == 0 ? 1
                        : you.attribute[ATTR_CHANNEL_DURATION]);

    // Don't stop on non-wait for the first turn of a channelled spell, since
    // that was the turn we cast it on.
    if (turn > 1 && crawl_state.prev_cmd != CMD_WAIT || !can_cast_spells(true))
    {
        stop_channelling_spells();
        return;
    }

    if ((spell == SPELL_FLAME_WAVE || spell == SPELL_SEARING_RAY)
        && turn > 1 && !enough_mp(1, true))
    {
        mprf("Without enough magic to sustain it, your %s dissipates.",
             spell_title(spell));
        stop_channelling_spells(true);
        return;
    }

    switch (you.attribute[ATTR_CHANNELLED_SPELL])
    {
        case SPELL_MAXWELLS_COUPLING:
            handle_maxwells_coupling();
            return;

        case SPELL_FLAME_WAVE:
            handle_flame_wave(turn);
            return;

        case SPELL_SEARING_RAY:
            handle_searing_ray(you, turn);
            return;

        case SPELL_CLOCKWORK_BEE:
            handle_clockwork_bee_spell(turn);
            return;

        default:
            mprf(MSGCH_WARN, "Attempting to channel buggy spell: %s", spell_title(spell));
    }
}

void stop_channelling_spells(bool quiet)
{
    const spell_type spell = (spell_type)you.attribute[ATTR_CHANNELLED_SPELL];

    you.attribute[ATTR_CHANNELLED_SPELL] = 0;
    you.attribute[ATTR_CHANNEL_DURATION] = 0;

    if (quiet)
        return;

    switch (spell)
    {
        case SPELL_FLAME_WAVE:
            mpr("You stop channelling waves of flame.");
            break;

        case SPELL_SEARING_RAY:
            mpr("You stop channelling your searing ray.");
            break;

        case SPELL_MAXWELLS_COUPLING:
            mpr("The insufficient charge dissipates harmlessly.");
            break;

        case SPELL_CLOCKWORK_BEE:
            mpr("You stop assembling your clockwork bee.");
            break;

        default:
            break;
    }
}

// Prompts the player when casting a spell like Irradiate, while having enough
// contam that it could push them into yellow.
//
// Returns true if we should abort.
bool warn_about_contam_cost(int max_contam)
{
    if (!Options.warn_contam_cost || you.magic_contamination >= 1000)
        return false;

    const int mul = you.has_mutation(MUT_CONTAMINATION_SUSCEPTIBLE) ? 2 : 1;

    if (you.magic_contamination + (max_contam * mul) >= 1000)
        return !yesno("Casting this now could dangerously contaminate you. Continue?", true, 'n');

    return false;
}
