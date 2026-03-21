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

static const RivelString rivel_global_CAFE_NAME = (RivelString){"R" "i" "v" "e" "l" " " "R" "o" "a" "s" "t" "e" "r" "s", 14, NULL};
static const RivelString rivel_global_DAY_LABEL = (RivelString){"F" "r" "i" "d" "a" "y", 6, NULL};
static const int64_t rivel_global_MORNING_DRINKS = INT64_C(46);
static const int64_t rivel_global_AFTERNOON_DRINKS = INT64_C(58);
static const int64_t rivel_global_EVENING_DRINKS = INT64_C(31);
static const int64_t rivel_global_PASTRIES_SOLD = INT64_C(24);
static const int64_t rivel_global_HOURS_OPEN = INT64_C(8);
static const int64_t rivel_global_BEAN_BAGS_OPEN = INT64_C(8);
static const int64_t rivel_global_BEAN_BAGS_LEFT = INT64_C(2);
static const double rivel_global_DRINK_PRICE = 5.5;
static const double rivel_global_PASTRY_PRICE = 3.5;
static const int64_t rivel_global_DRINK_TARGET = INT64_C(120);
static const RivelString rivel_global_SUPPLIER_NOTE = (RivelString){"F" "r" "i" "d" "a" "y" " " "d" "e" "l" "i" "v" "e" "r" "y" " " "c" "o" "n" "f" "i" "r" "m" "e" "d" ":" " " "o" "a" "t" " " "m" "i" "l" "k" " " "a" "n" "d" " " "b" "e" "a" "n" "s", 45, NULL};
static const RivelString rivel_global_REPORT_TITLE = (RivelString){"R" "i" "v" "e" "l" " " "R" "o" "a" "s" "t" "e" "r" "s" " " "c" "l" "o" "s" "i" "n" "g" " " "r" "e" "p" "o" "r" "t", 29, NULL};
static const RivelString rivel_global_BRAND_CODE = (RivelString){"R" "i" "v" "e" "l", 5, NULL};
static const int64_t rivel_global_DAY_LABEL_WIDTH = INT64_C(6);
static const bool rivel_global_NOTE_HAS_MILK = true;
static const bool rivel_global_BRAND_LOOKS_RIGHT = true;
static const bool rivel_global_NAME_HAS_ROASTERS = true;

static int64_t rivel_fn_main(void);
static int64_t rivel_fn_total_drinks_sold(void);
static int64_t rivel_fn_estimate_pastry_batches(int64_t rivel_param_pastries);
static double rivel_fn_drink_revenue(int64_t rivel_param_drinks);
static double rivel_fn_pastry_revenue(int64_t rivel_param_pastries);
static double rivel_fn_average_revenue_per_hour(double rivel_param_total_revenue);
static RivelString rivel_fn_closing_status(int64_t rivel_param_total_drinks, double rivel_param_gross_revenue, int64_t rivel_param_bean_bags_left);
static int64_t rivel_fn_emit_report(int64_t rivel_param_total_drinks, int64_t rivel_param_pastry_batches, int64_t rivel_param_bean_bags_used, double rivel_param_drink_total, double rivel_param_pastry_total, double rivel_param_gross_total, double rivel_param_hourly_average, RivelString rivel_param_status, int64_t rivel_param_checklist_count);
static RivelString rivel_fn_checklist_line(int64_t rivel_param_step);
static int64_t rivel_fn_print_tomorrow_checklist(void);

static int64_t rivel_fn_main(void) {
    rivel_print_string_take(rivel_string_retain(rivel_global_REPORT_TITLE));
    rivel_print_string_take(rivel_string_concat_take((RivelString){"D" "a" "y" ":" " ", 5, NULL}, rivel_string_retain(rivel_global_DAY_LABEL)));
    rivel_print_string_take(rivel_string_concat_take((RivelString){"B" "r" "a" "n" "d" " " "c" "o" "d" "e" ":" " ", 12, NULL}, rivel_string_retain(rivel_global_BRAND_CODE)));
    rivel_print_string_take(rivel_string_retain(rivel_global_SUPPLIER_NOTE));
    int64_t rivel_local_total_drinks_0 = rivel_fn_total_drinks_sold();
    int64_t rivel_local_pastry_batches_1 = rivel_fn_estimate_pastry_batches(rivel_global_PASTRIES_SOLD);
    int64_t rivel_local_bean_bags_used_2 = (rivel_global_BEAN_BAGS_OPEN - rivel_global_BEAN_BAGS_LEFT);
    double rivel_local_drink_total_3 = rivel_fn_drink_revenue(rivel_local_total_drinks_0);
    double rivel_local_pastry_total_4 = rivel_fn_pastry_revenue(rivel_global_PASTRIES_SOLD);
    double rivel_local_gross_total_5 = (rivel_local_drink_total_3 + rivel_local_pastry_total_4);
    double rivel_local_hourly_average_6 = rivel_fn_average_revenue_per_hour(rivel_local_gross_total_5);
    RivelString rivel_local_status_7 = rivel_fn_closing_status(rivel_local_total_drinks_0, rivel_local_gross_total_5, rivel_global_BEAN_BAGS_LEFT);
    int64_t rivel_local_checklist_count_8 = INT64_C(3);
    int64_t rivel_local_report_result_9 = rivel_fn_emit_report(rivel_local_total_drinks_0, rivel_local_pastry_batches_1, rivel_local_bean_bags_used_2, rivel_local_drink_total_3, rivel_local_pastry_total_4, rivel_local_gross_total_5, rivel_local_hourly_average_6, rivel_string_retain(rivel_local_status_7), rivel_local_checklist_count_8);
    if (rivel_local_report_result_9 != INT64_C(0)) {
        int64_t rivel_return_value_10 = rivel_local_report_result_9;
        rivel_string_release(rivel_local_status_7);
        return rivel_return_value_10;
    }
    rivel_print_string_take((RivelString){"T" "o" "m" "o" "r" "r" "o" "w" " " "c" "h" "e" "c" "k" "l" "i" "s" "t", 18, NULL});
    int64_t rivel_local_printed_checklist_count_11 = rivel_fn_print_tomorrow_checklist();
    if (rivel_local_printed_checklist_count_11 == rivel_local_checklist_count_8) {
        int64_t rivel_return_value_12 = INT64_C(0);
        return rivel_return_value_12;
    }
    else {
        int64_t rivel_return_value_13 = INT64_C(1);
        return rivel_return_value_13;
    }
}

static int64_t rivel_fn_total_drinks_sold(void) {
    int64_t rivel_return_value_0 = ((rivel_global_MORNING_DRINKS + rivel_global_AFTERNOON_DRINKS) + rivel_global_EVENING_DRINKS);
    return rivel_return_value_0;
}

static int64_t rivel_fn_estimate_pastry_batches(int64_t rivel_param_pastries) {
    int64_t rivel_local_batch_capacity_0 = INT64_C(8);
    int64_t rivel_local_batches_1 = INT64_C(0);
    int64_t rivel_local_covered_2 = INT64_C(0);
    while (rivel_local_covered_2 < rivel_param_pastries) {
        rivel_local_batches_1 = (rivel_local_batches_1 + INT64_C(1));
        rivel_local_covered_2 = (rivel_local_covered_2 + rivel_local_batch_capacity_0);
    }
    int64_t rivel_return_value_3 = rivel_local_batches_1;
    return rivel_return_value_3;
}

static double rivel_fn_drink_revenue(int64_t rivel_param_drinks) {
    double rivel_return_value_0 = (rivel_param_drinks * rivel_global_DRINK_PRICE);
    return rivel_return_value_0;
}

static double rivel_fn_pastry_revenue(int64_t rivel_param_pastries) {
    double rivel_return_value_0 = (rivel_param_pastries * rivel_global_PASTRY_PRICE);
    return rivel_return_value_0;
}

static double rivel_fn_average_revenue_per_hour(double rivel_param_total_revenue) {
    double rivel_return_value_0 = (rivel_param_total_revenue / rivel_global_HOURS_OPEN);
    return rivel_return_value_0;
}

static RivelString rivel_fn_closing_status(int64_t rivel_param_total_drinks, double rivel_param_gross_revenue, int64_t rivel_param_bean_bags_left) {
    if ((rivel_param_total_drinks >= rivel_global_DRINK_TARGET) && (rivel_param_gross_revenue >= 800.0)) {
        RivelString rivel_return_value_0 = (RivelString){"S" "t" "r" "o" "n" "g" " " "c" "l" "o" "s" "e" ":" " " "t" "h" "e" " " "c" "a" "f" "e" " " "b" "e" "a" "t" " " "i" "t" "s" " " "d" "r" "i" "n" "k" " " "t" "a" "r" "g" "e" "t" " " "a" "n" "d" " " "c" "l" "e" "a" "r" "e" "d" " " "a" " " "h" "e" "a" "l" "t" "h" "y" " " "n" "i" "g" "h" "t" ".", 73, NULL};
        return rivel_return_value_0;
    }
    else if ((rivel_param_bean_bags_left <= INT64_C(2)) || (!rivel_global_NOTE_HAS_MILK)) {
        RivelString rivel_return_value_1 = (RivelString){"W" "a" "t" "c" "h" " " "i" "n" "v" "e" "n" "t" "o" "r" "y" ":" " " "s" "a" "l" "e" "s" " " "w" "e" "r" "e" " " "s" "o" "l" "i" "d" "," " " "b" "u" "t" " " "m" "o" "r" "n" "i" "n" "g" " " "p" "r" "e" "p" " " "n" "e" "e" "d" "s" " " "a" "t" "t" "e" "n" "t" "i" "o" "n" ".", 68, NULL};
        return rivel_return_value_1;
    }
    else {
        RivelString rivel_return_value_2 = (RivelString){"M" "i" "s" "s" "e" "d" " " "t" "a" "r" "g" "e" "t" ":" " " "t" "h" "e" " " "o" "p" "e" "n" "e" "r" " " "s" "h" "o" "u" "l" "d" " " "p" "u" "s" "h" " " "b" "r" "e" "a" "k" "f" "a" "s" "t" " " "t" "r" "a" "f" "f" "i" "c" " " "h" "a" "r" "d" "e" "r" " " "t" "o" "m" "o" "r" "r" "o" "w" ".", 72, NULL};
        return rivel_return_value_2;
    }
}

static int64_t rivel_fn_emit_report(int64_t rivel_param_total_drinks, int64_t rivel_param_pastry_batches, int64_t rivel_param_bean_bags_used, double rivel_param_drink_total, double rivel_param_pastry_total, double rivel_param_gross_total, double rivel_param_hourly_average, RivelString rivel_param_status, int64_t rivel_param_checklist_count) {
    rivel_print_string_take((RivelString){"O" "p" "e" "r" "a" "t" "i" "o" "n" "s" " " "t" "o" "t" "a" "l" "s", 17, NULL});
    rivel_print_string_take((RivelString){"D" "a" "y" " " "l" "a" "b" "e" "l" " " "w" "i" "d" "t" "h", 15, NULL});
    rivel_print_int(rivel_global_DAY_LABEL_WIDTH);
    rivel_print_string_take((RivelString){"D" "r" "i" "n" "k" "s" " " "s" "o" "l" "d", 11, NULL});
    rivel_print_int(rivel_param_total_drinks);
    rivel_print_string_take((RivelString){"P" "a" "s" "t" "r" "i" "e" "s" " " "s" "o" "l" "d", 13, NULL});
    rivel_print_int(rivel_global_PASTRIES_SOLD);
    rivel_print_string_take((RivelString){"P" "a" "s" "t" "r" "y" " " "b" "a" "t" "c" "h" "e" "s" " " "b" "a" "k" "e" "d", 20, NULL});
    rivel_print_int(rivel_param_pastry_batches);
    rivel_print_string_take((RivelString){"B" "e" "a" "n" " " "b" "a" "g" "s" " " "u" "s" "e" "d", 14, NULL});
    rivel_print_int(rivel_param_bean_bags_used);
    rivel_print_string_take((RivelString){"F" "i" "n" "a" "n" "c" "i" "a" "l" " " "s" "u" "m" "m" "a" "r" "y", 17, NULL});
    rivel_print_string_take((RivelString){"D" "r" "i" "n" "k" " " "r" "e" "v" "e" "n" "u" "e", 13, NULL});
    rivel_print_double(rivel_param_drink_total);
    rivel_print_string_take((RivelString){"P" "a" "s" "t" "r" "y" " " "r" "e" "v" "e" "n" "u" "e", 14, NULL});
    rivel_print_double(rivel_param_pastry_total);
    rivel_print_string_take((RivelString){"G" "r" "o" "s" "s" " " "r" "e" "v" "e" "n" "u" "e", 13, NULL});
    rivel_print_double(rivel_param_gross_total);
    rivel_print_string_take((RivelString){"A" "v" "e" "r" "a" "g" "e" " " "r" "e" "v" "e" "n" "u" "e" " " "p" "e" "r" " " "h" "o" "u" "r", 24, NULL});
    rivel_print_double(rivel_param_hourly_average);
    rivel_print_string_take((RivelString){"C" "l" "o" "s" "i" "n" "g" " " "s" "t" "a" "t" "u" "s", 14, NULL});
    rivel_print_string_take(rivel_string_retain(rivel_param_status));
    rivel_print_string_take((RivelString){"S" "u" "p" "p" "l" "i" "e" "r" " " "n" "o" "t" "e" " " "m" "e" "n" "t" "i" "o" "n" "s" " " "m" "i" "l" "k", 27, NULL});
    rivel_print_bool(rivel_global_NOTE_HAS_MILK);
    rivel_print_string_take((RivelString){"C" "a" "f" "e" " " "n" "a" "m" "e" " " "s" "t" "a" "r" "t" "s" " " "c" "o" "r" "r" "e" "c" "t" "l" "y", 26, NULL});
    rivel_print_bool(rivel_global_BRAND_LOOKS_RIGHT);
    rivel_print_string_take((RivelString){"C" "a" "f" "e" " " "n" "a" "m" "e" " " "e" "n" "d" "s" " " "c" "o" "r" "r" "e" "c" "t" "l" "y", 24, NULL});
    rivel_print_bool(rivel_global_NAME_HAS_ROASTERS);
    rivel_print_string_take((RivelString){"C" "h" "e" "c" "k" "l" "i" "s" "t" " " "i" "t" "e" "m" "s" " " "p" "l" "a" "n" "n" "e" "d", 23, NULL});
    rivel_print_int(rivel_param_checklist_count);
    int64_t rivel_return_value_0 = INT64_C(0);
    rivel_string_release(rivel_param_status);
    return rivel_return_value_0;
}

static RivelString rivel_fn_checklist_line(int64_t rivel_param_step) {
    if (rivel_param_step == INT64_C(1)) {
        RivelString rivel_return_value_0 = (RivelString){"1" "." " " "R" "e" "s" "t" "o" "c" "k" " " "o" "a" "t" " " "m" "i" "l" "k" " " "a" "n" "d" " " "l" "a" "b" "e" "l" " " "t" "h" "e" " " "m" "o" "r" "n" "i" "n" "g" " " "f" "r" "i" "d" "g" "e" ".", 49, NULL};
        return rivel_return_value_0;
    }
    else if (rivel_param_step == INT64_C(2)) {
        RivelString rivel_return_value_1 = (RivelString){"2" "." " " "G" "r" "i" "n" "d" " " "t" "h" "e" " " "f" "i" "r" "s" "t" " " "h" "o" "p" "p" "e" "r" " " "a" "n" "d" " " "s" "t" "a" "g" "e" " " "p" "a" "s" "t" "r" "y" " " "b" "a" "g" "s" ".", 48, NULL};
        return rivel_return_value_1;
    }
    else {
        RivelString rivel_return_value_2 = (RivelString){"3" "." " " "W" "r" "i" "t" "e" " " "t" "h" "e" " " "w" "e" "e" "k" "e" "n" "d" " " "s" "p" "e" "c" "i" "a" "l" " " "b" "o" "a" "r" "d" " " "b" "e" "f" "o" "r" "e" " " "t" "h" "e" " " "f" "i" "r" "s" "t" " " "r" "u" "s" "h" ".", 57, NULL};
        return rivel_return_value_2;
    }
}

static int64_t rivel_fn_print_tomorrow_checklist(void) {
    int64_t rivel_local_printed_0 = INT64_C(0);
    {
        int64_t rivel_range_start_1 = INT64_C(1);
        int64_t rivel_range_end_2 = INT64_C(3);
        if (rivel_range_start_1 <= rivel_range_end_2) {
            int64_t rivel_local_step_3 = rivel_range_start_1;
            while (true) {
                rivel_print_string_take(rivel_fn_checklist_line(rivel_local_step_3));
                rivel_local_printed_0 = (rivel_local_printed_0 + INT64_C(1));
                if (rivel_local_step_3 == rivel_range_end_2) {
                    break;
                }
                rivel_local_step_3 += INT64_C(1);
            }
        }
    }
    int64_t rivel_return_value_4 = rivel_local_printed_0;
    return rivel_return_value_4;
}

int main(void) {
    return rivel_exit_code(rivel_fn_main());
}
