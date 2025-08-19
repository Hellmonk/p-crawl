/**
 * @file
 * @brief Zot clock functions.
**/

#pragma once

// After how many auts without visiting new floors does the player suffer mhp loss?
const int MAX_ZOT_CLOCK = 500 * BASELINE_DELAY;

bool bezotted();
bool bezotted_in(branch_type br);
bool should_fear_zot();
bool zot_immune();
bool zot_clock_active();
int bezotting_level();
int bezotting_level_in(branch_type br);
int turns_until_zot();
int turns_until_zot_in(branch_type br);
void reset_zot_clock();
void incr_zot_clock();
void set_turns_until_zot(int turns_left);

bool gem_clock_active();
void incr_gem_clock();
void print_gem_warnings(int gem, int old_time_taken);
void maybe_break_floor_gem();
void shatter_floor_gem(bool quiet = false);
int gem_time_left(int gem);
string gem_status();
