#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rivel_exit_code(int64_t value) {
    return (int)value;
}
static void rivel_check_divisor(int64_t rhs) {
    if (rhs == INT64_C(0)) {
        fputs("division by zero\n", stderr);
        exit(1);
    }
}

static int64_t rivel_div(int64_t lhs, int64_t rhs) {
    rivel_check_divisor(rhs);
    return lhs / rhs;
}

static int64_t rivel_mod(int64_t lhs, int64_t rhs) {
    rivel_check_divisor(rhs);
    return lhs % rhs;
}

typedef struct RivelStringStorage RivelStringStorage;

struct RivelStringStorage {
    size_t refcount;
    size_t len;
    char bytes[];
};

typedef struct {
    const char *data;
    size_t len;
    RivelStringStorage *storage;
} RivelString;

static RivelStringStorage *rivel_string_storage_new(size_t len) {
    RivelStringStorage *storage = (RivelStringStorage *)malloc(sizeof(*storage) + len);
    if (storage == NULL) {
        fputs("out of memory\n", stderr);
        exit(1);
    }
    storage->refcount = 1U;
    storage->len = len;
    return storage;
}

static RivelString rivel_string_retain(RivelString value) {
    if (value.storage != NULL) {
        value.storage->refcount += 1U;
    }
    return value;
}

static void rivel_string_release(RivelString value) {
    if (value.storage == NULL) {
        return;
    }
    if (value.storage->refcount == 1U) {
        free(value.storage);
    } else {
        value.storage->refcount -= 1U;
    }
}

static RivelString rivel_string_copy(const char *data, size_t len) {
    RivelStringStorage *storage;
    if (len == 0U) {
        return (RivelString){"", 0U, NULL};
    }
    storage = rivel_string_storage_new(len);
    memcpy(storage->bytes, data, len);
    return (RivelString){storage->bytes, len, storage};
}

static void rivel_substring_out_of_range(void) {
    fputs("substring out of range\n", stderr);
    exit(1);
}

static bool rivel_string_equal(RivelString lhs, RivelString rhs) {
    if (lhs.len != rhs.len) {
        return false;
    }
    if (lhs.len == 0U) {
        return true;
    }
    return memcmp(lhs.data, rhs.data, lhs.len) == 0;
}

static bool rivel_string_contains(RivelString haystack, RivelString needle) {
    size_t index = 0U;
    if (needle.len == 0U) {
        return true;
    }
    if (needle.len > haystack.len) {
        return false;
    }
    while (index + needle.len <= haystack.len) {
        if (memcmp(haystack.data + index, needle.data, needle.len) == 0) {
            return true;
        }
        index += 1U;
    }
    return false;
}

static bool rivel_string_starts_with(RivelString value, RivelString prefix) {
    if (prefix.len > value.len) {
        return false;
    }
    if (prefix.len == 0U) {
        return true;
    }
    return memcmp(value.data, prefix.data, prefix.len) == 0;
}

static bool rivel_string_ends_with(RivelString value, RivelString suffix) {
    if (suffix.len > value.len) {
        return false;
    }
    if (suffix.len == 0U) {
        return true;
    }
    return memcmp(value.data + value.len - suffix.len, suffix.data, suffix.len) == 0;
}

static RivelString rivel_string_concat_take(RivelString lhs, RivelString rhs) {
    size_t len = lhs.len + rhs.len;
    RivelString result;
    if (len == 0U) {
        result = (RivelString){"", 0U, NULL};
    } else {
        RivelStringStorage *storage = rivel_string_storage_new(len);
        if (lhs.len > 0U) {
            memcpy(storage->bytes, lhs.data, lhs.len);
        }
        if (rhs.len > 0U) {
            memcpy(storage->bytes + lhs.len, rhs.data, rhs.len);
        }
        result = (RivelString){storage->bytes, len, storage};
    }
    rivel_string_release(lhs);
    rivel_string_release(rhs);
    return result;
}

static bool rivel_string_eq_take(RivelString lhs, RivelString rhs) {
    bool result = rivel_string_equal(lhs, rhs);
    rivel_string_release(lhs);
    rivel_string_release(rhs);
    return result;
}

static int64_t rivel_string_len_take(RivelString value) {
    int64_t len = (int64_t)value.len;
    rivel_string_release(value);
    return len;
}

static RivelString rivel_string_substr_take(RivelString value, int64_t start_value, int64_t len_value) {
    size_t start_index;
    size_t len;
    RivelString result;
    if (start_value < 0 || len_value < 0) {
        rivel_string_release(value);
        rivel_substring_out_of_range();
    }
    start_index = (size_t)start_value;
    len = (size_t)len_value;
    if (start_index > value.len || len > value.len - start_index) {
        rivel_string_release(value);
        rivel_substring_out_of_range();
    }
    result.data = value.data + start_index;
    result.len = len;
    result.storage = value.storage;
    if (result.storage != NULL) {
        result.storage->refcount += 1U;
    }
    rivel_string_release(value);
    return result;
}

static bool rivel_string_contains_take(RivelString haystack, RivelString needle) {
    bool result = rivel_string_contains(haystack, needle);
    rivel_string_release(haystack);
    rivel_string_release(needle);
    return result;
}

static bool rivel_string_starts_with_take(RivelString value, RivelString prefix) {
    bool result = rivel_string_starts_with(value, prefix);
    rivel_string_release(value);
    rivel_string_release(prefix);
    return result;
}

static bool rivel_string_ends_with_take(RivelString value, RivelString suffix) {
    bool result = rivel_string_ends_with(value, suffix);
    rivel_string_release(value);
    rivel_string_release(suffix);
    return result;
}

static void rivel_print_int(int64_t value) {
    printf("%lld\n", (long long)value);
}

static void rivel_print_bool(bool value) {
    puts(value ? "true" : "false");
}

static void rivel_print_double(double value) {
    printf("%.17g\n", value);
}

static void rivel_print_string_take(RivelString value) {
    if (value.len > 0U) {
        fwrite(value.data, 1U, value.len, stdout);
    }
    fputc('\n', stdout);
    rivel_string_release(value);
}

typedef struct RivelStruct_Stats {
    int64_t total;
    double average;
    RivelString tier;
} RivelStruct_Stats;

static RivelStruct_Stats rivel_struct_Stats_retain(RivelStruct_Stats value) {
    value.tier = rivel_string_retain(value.tier);
    return value;
}

static void rivel_struct_Stats_release(RivelStruct_Stats value) {
    rivel_string_release(value.tier);
}

static int64_t rivel_struct_Stats_take_total(RivelStruct_Stats value) {
    int64_t field = value.total;
    rivel_string_release(value.tier);
    return field;
}

static double rivel_struct_Stats_take_average(RivelStruct_Stats value) {
    double field = value.average;
    rivel_string_release(value.tier);
    return field;
}

static RivelString rivel_struct_Stats_take_tier(RivelStruct_Stats value) {
    RivelString field = rivel_string_retain(value.tier);
    rivel_string_release(value.tier);
    return field;
}

typedef struct RivelStruct_Profile {
    RivelString name;
    int64_t level;
    bool active;
    RivelStruct_Stats stats;
} RivelStruct_Profile;

static RivelStruct_Profile rivel_struct_Profile_retain(RivelStruct_Profile value) {
    value.name = rivel_string_retain(value.name);
    value.stats = rivel_struct_Stats_retain(value.stats);
    return value;
}

static void rivel_struct_Profile_release(RivelStruct_Profile value) {
    rivel_string_release(value.name);
    rivel_struct_Stats_release(value.stats);
}

static RivelString rivel_struct_Profile_take_name(RivelStruct_Profile value) {
    RivelString field = rivel_string_retain(value.name);
    rivel_string_release(value.name);
    rivel_struct_Stats_release(value.stats);
    return field;
}

static int64_t rivel_struct_Profile_take_level(RivelStruct_Profile value) {
    int64_t field = value.level;
    rivel_string_release(value.name);
    rivel_struct_Stats_release(value.stats);
    return field;
}

static bool rivel_struct_Profile_take_active(RivelStruct_Profile value) {
    bool field = value.active;
    rivel_string_release(value.name);
    rivel_struct_Stats_release(value.stats);
    return field;
}

static RivelStruct_Stats rivel_struct_Profile_take_stats(RivelStruct_Profile value) {
    RivelStruct_Stats field = rivel_struct_Stats_retain(value.stats);
    rivel_string_release(value.name);
    rivel_struct_Stats_release(value.stats);
    return field;
}


static const RivelString rivel_global_TITLE = (RivelString){"R" "i" "v" "e" "l" " " "f" "e" "a" "t" "u" "r" "e" " " "t" "o" "u" "r", 18, NULL};
static const RivelString rivel_global_TAGLINE = (RivelString){"s" "m" "a" "l" "l" " " "l" "a" "n" "g" "u" "a" "g" "e" "," " " "c" "l" "e" "a" "r" " " "s" "y" "n" "t" "a" "x", 28, NULL};
static const RivelString rivel_global_TITLE_SLICE = (RivelString){"f" "e" "a" "t" "u" "r" "e", 7, NULL};
static const int64_t rivel_global_TITLE_WIDTH = INT64_C(18);
static const bool rivel_global_HAS_RIVEL = true;
static const bool rivel_global_LOOKS_POLISHED = true;

static RivelString rivel_fn_section(RivelString rivel_param_name);
static int64_t rivel_fn_sum_inclusive(int64_t rivel_param_limit);
static int64_t rivel_fn_sum_exclusive(int64_t rivel_param_limit);
static int64_t rivel_fn_spin_until(int64_t rivel_param_limit);
static double rivel_fn_mean(int64_t rivel_param_left, int64_t rivel_param_right);
static RivelString rivel_fn_tier_for(int64_t rivel_param_total);
static RivelStruct_Stats rivel_fn_build_stats(int64_t rivel_param_rounds);
static RivelStruct_Profile rivel_fn_make_profile(RivelString rivel_param_name, int64_t rivel_param_rounds);
static RivelStruct_Profile rivel_fn_promote(RivelStruct_Profile rivel_param_profile, int64_t rivel_param_bonus);
static RivelString rivel_fn_summary(RivelStruct_Profile rivel_param_profile);
static int64_t rivel_fn_main(void);

static RivelString rivel_fn_section(RivelString rivel_param_name) {
    RivelString rivel_return_value_0 = rivel_string_concat_take(rivel_string_concat_take((RivelString){"=" "=" " ", 3, NULL}, rivel_string_retain(rivel_param_name)), (RivelString){" " "=" "=", 3, NULL});
    rivel_string_release(rivel_param_name);
    return rivel_return_value_0;
}

static int64_t rivel_fn_sum_inclusive(int64_t rivel_param_limit) {
    int64_t rivel_local_total_0 = INT64_C(0);
    {
        int64_t rivel_range_start_1 = INT64_C(1);
        int64_t rivel_range_end_2 = rivel_param_limit;
        if (rivel_range_start_1 <= rivel_range_end_2) {
            int64_t rivel_local_i_3 = rivel_range_start_1;
            while (true) {
                rivel_local_total_0 = (rivel_local_total_0 + rivel_local_i_3);
                if (rivel_local_i_3 == rivel_range_end_2) {
                    break;
                }
                rivel_local_i_3 += INT64_C(1);
            }
        }
    }
    int64_t rivel_return_value_4 = rivel_local_total_0;
    return rivel_return_value_4;
}

static int64_t rivel_fn_sum_exclusive(int64_t rivel_param_limit) {
    int64_t rivel_local_total_0 = INT64_C(0);
    {
        int64_t rivel_range_start_1 = INT64_C(0);
        int64_t rivel_range_end_2 = rivel_param_limit;
        if (rivel_range_start_1 < rivel_range_end_2) {
            int64_t rivel_local_i_3 = rivel_range_start_1;
            while (rivel_local_i_3 < rivel_range_end_2) {
                rivel_local_total_0 = (rivel_local_total_0 + rivel_local_i_3);
                rivel_local_i_3 += INT64_C(1);
            }
        }
    }
    int64_t rivel_return_value_4 = rivel_local_total_0;
    return rivel_return_value_4;
}

static int64_t rivel_fn_spin_until(int64_t rivel_param_limit) {
    int64_t rivel_local_step_0 = INT64_C(0);
    int64_t rivel_local_total_1 = INT64_C(0);
    while (rivel_local_step_0 < rivel_param_limit) {
        rivel_local_total_1 = (rivel_local_total_1 + (rivel_local_step_0 * INT64_C(2)));
        rivel_local_step_0 = (rivel_local_step_0 + INT64_C(1));
    }
    int64_t rivel_return_value_2 = rivel_local_total_1;
    return rivel_return_value_2;
}

static double rivel_fn_mean(int64_t rivel_param_left, int64_t rivel_param_right) {
    double rivel_return_value_0 = ((rivel_param_left + rivel_param_right) / 2.0);
    return rivel_return_value_0;
}

static RivelString rivel_fn_tier_for(int64_t rivel_param_total) {
    if (rivel_param_total >= INT64_C(15)) {
        RivelString rivel_return_value_0 = (RivelString){"e" "x" "p" "e" "r" "t", 6, NULL};
        return rivel_return_value_0;
    }
    else if (rivel_param_total >= INT64_C(8)) {
        RivelString rivel_return_value_1 = (RivelString){"s" "t" "e" "a" "d" "y", 6, NULL};
        return rivel_return_value_1;
    }
    else {
        RivelString rivel_return_value_2 = (RivelString){"s" "t" "a" "r" "t" "e" "r", 7, NULL};
        return rivel_return_value_2;
    }
}

static RivelStruct_Stats rivel_fn_build_stats(int64_t rivel_param_rounds) {
    int64_t rivel_local_total_0 = rivel_fn_sum_inclusive(rivel_param_rounds);
    double rivel_local_average_1 = ((rivel_local_total_0 * 1.0) / rivel_param_rounds);
    RivelStruct_Stats rivel_return_value_2 = ((RivelStruct_Stats){.total = rivel_local_total_0, .average = rivel_local_average_1, .tier = rivel_fn_tier_for(rivel_local_total_0)});
    return rivel_return_value_2;
}

static RivelStruct_Profile rivel_fn_make_profile(RivelString rivel_param_name, int64_t rivel_param_rounds) {
    RivelStruct_Profile rivel_return_value_0 = ((RivelStruct_Profile){.name = rivel_string_retain(rivel_param_name), .level = INT64_C(1), .active = false, .stats = rivel_fn_build_stats(rivel_param_rounds)});
    rivel_string_release(rivel_param_name);
    return rivel_return_value_0;
}

static RivelStruct_Profile rivel_fn_promote(RivelStruct_Profile rivel_param_profile, int64_t rivel_param_bonus) {
    RivelStruct_Profile rivel_local_next_0 = rivel_struct_Profile_retain(rivel_param_profile);
    rivel_local_next_0.level = (rivel_struct_Profile_take_level(rivel_struct_Profile_retain(rivel_local_next_0)) + rivel_param_bonus);
    rivel_local_next_0.active = (rivel_struct_Profile_take_level(rivel_struct_Profile_retain(rivel_local_next_0)) >= INT64_C(2));
    RivelStruct_Profile rivel_return_value_1 = rivel_struct_Profile_retain(rivel_local_next_0);
    rivel_struct_Profile_release(rivel_local_next_0);
    rivel_struct_Profile_release(rivel_param_profile);
    return rivel_return_value_1;
}

static RivelString rivel_fn_summary(RivelStruct_Profile rivel_param_profile) {
    if (rivel_struct_Profile_take_active(rivel_struct_Profile_retain(rivel_param_profile)) && (rivel_struct_Stats_take_total(rivel_struct_Profile_take_stats(rivel_struct_Profile_retain(rivel_param_profile))) >= INT64_C(10))) {
        RivelString rivel_return_value_0 = rivel_string_concat_take(rivel_struct_Profile_take_name(rivel_struct_Profile_retain(rivel_param_profile)), (RivelString){" " "l" "o" "o" "k" "s" " " "r" "e" "a" "d" "y", 12, NULL});
        rivel_struct_Profile_release(rivel_param_profile);
        return rivel_return_value_0;
    }
    else if (rivel_struct_Profile_take_active(rivel_struct_Profile_retain(rivel_param_profile))) {
        RivelString rivel_return_value_1 = rivel_string_concat_take(rivel_struct_Profile_take_name(rivel_struct_Profile_retain(rivel_param_profile)), (RivelString){" " "i" "s" " " "i" "m" "p" "r" "o" "v" "i" "n" "g", 13, NULL});
        return rivel_return_value_1;
    }
    else {
        RivelString rivel_return_value_2 = rivel_string_concat_take(rivel_struct_Profile_take_name(rivel_struct_Profile_retain(rivel_param_profile)), (RivelString){" " "i" "s" " " "w" "a" "r" "m" "i" "n" "g" " " "u" "p", 14, NULL});
        return rivel_return_value_2;
    }
}

static int64_t rivel_fn_main(void) {
    rivel_print_string_take(rivel_fn_section(rivel_string_retain(rivel_global_TITLE)));
    rivel_print_string_take(rivel_string_retain(rivel_global_TAGLINE));
    rivel_print_string_take((RivelString){"s" "l" "i" "c" "e" " " "f" "r" "o" "m" " " "t" "h" "e" " " "t" "i" "t" "l" "e", 20, NULL});
    rivel_print_string_take(rivel_string_retain(rivel_global_TITLE_SLICE));
    rivel_print_string_take((RivelString){"t" "i" "t" "l" "e" " " "w" "i" "d" "t" "h", 11, NULL});
    rivel_print_int(rivel_global_TITLE_WIDTH);
    rivel_print_string_take((RivelString){"c" "o" "n" "t" "a" "i" "n" "s" " " "R" "i" "v" "e" "l", 14, NULL});
    rivel_print_bool(rivel_global_HAS_RIVEL);
    rivel_print_string_take((RivelString){"s" "t" "a" "r" "t" "s" " " "a" "n" "d" " " "e" "n" "d" "s" " " "w" "e" "l" "l", 20, NULL});
    rivel_print_bool(rivel_global_LOOKS_POLISHED);
    rivel_print_string_take(rivel_fn_section((RivelString){"l" "o" "o" "p" "s" " " "a" "n" "d" " " "n" "u" "m" "b" "e" "r" "s", 17, NULL}));
    int64_t rivel_local_inclusive_0 = rivel_fn_sum_inclusive(INT64_C(5));
    int64_t rivel_local_exclusive_1 = rivel_fn_sum_exclusive(INT64_C(5));
    int64_t rivel_local_spun_2 = rivel_fn_spin_until(INT64_C(4));
    double rivel_local_blended_3 = rivel_fn_mean(rivel_local_inclusive_0, (rivel_local_exclusive_1 + rivel_local_spun_2));
    rivel_print_string_take((RivelString){"i" "n" "c" "l" "u" "s" "i" "v" "e" " " "f" "o" "r" " " "t" "o" "t" "a" "l", 19, NULL});
    rivel_print_int(rivel_local_inclusive_0);
    rivel_print_string_take((RivelString){"e" "x" "c" "l" "u" "s" "i" "v" "e" " " "f" "o" "r" " " "t" "o" "t" "a" "l", 19, NULL});
    rivel_print_int(rivel_local_exclusive_1);
    rivel_print_string_take((RivelString){"w" "h" "i" "l" "e" " " "t" "o" "t" "a" "l", 11, NULL});
    rivel_print_int(rivel_local_spun_2);
    rivel_print_string_take((RivelString){"d" "o" "u" "b" "l" "e" " " "m" "e" "a" "n", 11, NULL});
    rivel_print_double(rivel_local_blended_3);
    rivel_print_string_take(rivel_fn_section((RivelString){"s" "t" "r" "u" "c" "t" "s" " " "a" "n" "d" " " "f" "u" "n" "c" "t" "i" "o" "n" "s", 21, NULL}));
    RivelStruct_Profile rivel_local_rookie_4 = rivel_fn_make_profile((RivelString){"N" "o" "v" "a", 4, NULL}, INT64_C(4));
    RivelStruct_Profile rivel_local_upgraded_5 = rivel_fn_promote(rivel_struct_Profile_retain(rivel_local_rookie_4), INT64_C(2));
    rivel_print_string_take(rivel_struct_Profile_take_name(rivel_struct_Profile_retain(rivel_local_upgraded_5)));
    rivel_print_int(rivel_struct_Profile_take_level(rivel_struct_Profile_retain(rivel_local_upgraded_5)));
    rivel_print_bool(rivel_struct_Profile_take_active(rivel_struct_Profile_retain(rivel_local_upgraded_5)));
    rivel_print_string_take(rivel_struct_Stats_take_tier(rivel_struct_Profile_take_stats(rivel_struct_Profile_retain(rivel_local_upgraded_5))));
    rivel_print_int(rivel_struct_Stats_take_total(rivel_struct_Profile_take_stats(rivel_struct_Profile_retain(rivel_local_upgraded_5))));
    rivel_print_double(rivel_struct_Stats_take_average(rivel_struct_Profile_take_stats(rivel_struct_Profile_retain(rivel_local_upgraded_5))));
    rivel_print_string_take(rivel_fn_summary(rivel_struct_Profile_retain(rivel_local_upgraded_5)));
    int64_t rivel_return_value_6 = INT64_C(0);
    rivel_struct_Profile_release(rivel_local_upgraded_5);
    rivel_struct_Profile_release(rivel_local_rookie_4);
    return rivel_return_value_6;
}

int main(void) {
    return rivel_exit_code(rivel_fn_main());
}
