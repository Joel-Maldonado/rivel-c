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

static const int64_t rivel_global_TARGET_INDEX = INT64_C(8);
static const int64_t rivel_global_LIMIT = INT64_C(50);
static const bool rivel_global_REPORT_READY = true;

static int64_t rivel_fn_main(void);
static int64_t rivel_fn_fib(int64_t rivel_param_n);
static int64_t rivel_fn_sum_through(int64_t rivel_param_last_index);
static int64_t rivel_fn_first_index_at_least(int64_t rivel_param_limit);
static bool rivel_fn_is_even(int64_t rivel_param_value);
static int64_t rivel_fn_distance_from_limit(int64_t rivel_param_value, int64_t rivel_param_limit);
static bool rivel_fn_report_consistent(int64_t rivel_param_target, int64_t rivel_param_previous, int64_t rivel_param_total);
static int64_t rivel_fn_emit_report(int64_t rivel_param_target, int64_t rivel_param_previous, int64_t rivel_param_total, int64_t rivel_param_crossing_index);

static int64_t rivel_fn_main(void) {
    if (!rivel_global_REPORT_READY) {
        int64_t rivel_return_value_0 = INT64_C(1);
        return rivel_return_value_0;
    }
    int64_t rivel_local_target_1 = rivel_fn_fib(rivel_global_TARGET_INDEX);
    int64_t rivel_local_previous_2 = rivel_fn_fib((rivel_global_TARGET_INDEX - INT64_C(1)));
    int64_t rivel_local_total_3 = rivel_fn_sum_through(rivel_global_TARGET_INDEX);
    int64_t rivel_local_crossing_index_4 = rivel_fn_first_index_at_least(rivel_global_LIMIT);
    int64_t rivel_local_signed_distance_5 = rivel_fn_distance_from_limit(rivel_local_target_1, rivel_global_LIMIT);
    bool rivel_local_consistent_6 = rivel_fn_report_consistent(rivel_local_target_1, rivel_local_previous_2, rivel_local_total_3);
    rivel_fn_emit_report(rivel_local_target_1, rivel_local_previous_2, rivel_local_total_3, rivel_local_crossing_index_4);
    if (rivel_local_consistent_6 && (rivel_local_target_1 > rivel_local_previous_2)) {
        int64_t rivel_local_margin_7 = (rivel_local_target_1 - rivel_local_previous_2);
        rivel_print_int(rivel_local_margin_7);
    }
    else if (rivel_local_signed_distance_5 == INT64_C(0)) {
        rivel_print_int(INT64_C(0));
    }
    else {
        int64_t rivel_local_signed_distance_8 = (-rivel_local_signed_distance_5);
        rivel_print_int(rivel_local_signed_distance_8);
    }
    RivelString rivel_local_x_9 = (RivelString){"H" "e" "l" "l" "o", 5, NULL};
    {
        int64_t rivel_range_start_10 = INT64_C(1);
        int64_t rivel_range_end_11 = INT64_C(5);
        if (rivel_range_start_10 <= rivel_range_end_11) {
            int64_t rivel_local_i_12 = rivel_range_start_10;
            while (true) {
                rivel_print_string_take(rivel_string_retain(rivel_local_x_9));
                if (rivel_local_i_12 == rivel_range_end_11) {
                    break;
                }
                rivel_local_i_12 += INT64_C(1);
            }
        }
    }
    int64_t rivel_return_value_13 = rivel_local_target_1;
    rivel_string_release(rivel_local_x_9);
    return rivel_return_value_13;
}

static int64_t rivel_fn_fib(int64_t rivel_param_n) {
    if (rivel_param_n == INT64_C(0)) {
        int64_t rivel_return_value_0 = INT64_C(0);
        return rivel_return_value_0;
    }
    else if (rivel_param_n == INT64_C(1)) {
        int64_t rivel_return_value_1 = INT64_C(1);
        return rivel_return_value_1;
    }
    else {
        int64_t rivel_return_value_2 = (rivel_fn_fib((rivel_param_n - INT64_C(1))) + rivel_fn_fib((rivel_param_n - INT64_C(2))));
        return rivel_return_value_2;
    }
}

static int64_t rivel_fn_sum_through(int64_t rivel_param_last_index) {
    int64_t rivel_local_index_0 = INT64_C(0);
    int64_t rivel_local_total_1 = INT64_C(0);
    while (rivel_local_index_0 <= rivel_param_last_index) {
        rivel_local_total_1 = (rivel_local_total_1 + rivel_fn_fib(rivel_local_index_0));
        rivel_local_index_0 = (rivel_local_index_0 + INT64_C(1));
    }
    int64_t rivel_return_value_2 = rivel_local_total_1;
    return rivel_return_value_2;
}

static int64_t rivel_fn_first_index_at_least(int64_t rivel_param_limit) {
    int64_t rivel_local_index_0 = INT64_C(0);
    while (rivel_fn_fib(rivel_local_index_0) < rivel_param_limit) {
        rivel_local_index_0 = (rivel_local_index_0 + INT64_C(1));
    }
    int64_t rivel_return_value_1 = rivel_local_index_0;
    return rivel_return_value_1;
}

static bool rivel_fn_is_even(int64_t rivel_param_value) {
    bool rivel_return_value_0 = (rivel_mod(rivel_param_value, INT64_C(2)) == INT64_C(0));
    return rivel_return_value_0;
}

static int64_t rivel_fn_distance_from_limit(int64_t rivel_param_value, int64_t rivel_param_limit) {
    if (rivel_param_value == rivel_param_limit) {
        int64_t rivel_return_value_0 = INT64_C(0);
        return rivel_return_value_0;
    }
    else if (rivel_param_value < rivel_param_limit) {
        int64_t rivel_return_value_1 = (-(rivel_param_limit - rivel_param_value));
        return rivel_return_value_1;
    }
    else {
        int64_t rivel_return_value_2 = (rivel_param_value - rivel_param_limit);
        return rivel_return_value_2;
    }
}

static bool rivel_fn_report_consistent(int64_t rivel_param_target, int64_t rivel_param_previous, int64_t rivel_param_total) {
    int64_t rivel_local_doubled_previous_0 = (rivel_param_previous * INT64_C(2));
    int64_t rivel_local_average_1 = rivel_div(rivel_param_total, (rivel_global_TARGET_INDEX + INT64_C(1)));
    bool rivel_return_value_2 = (((rivel_param_target >= rivel_param_previous) && (rivel_param_target != INT64_C(0))) && ((rivel_local_doubled_previous_0 > rivel_param_target) || (rivel_local_average_1 > INT64_C(0))));
    return rivel_return_value_2;
}

static int64_t rivel_fn_emit_report(int64_t rivel_param_target, int64_t rivel_param_previous, int64_t rivel_param_total, int64_t rivel_param_crossing_index) {
    rivel_print_int(rivel_param_target);
    rivel_print_int(rivel_param_previous);
    rivel_print_int(rivel_param_total);
    rivel_print_int(rivel_param_crossing_index);
    rivel_print_bool(rivel_fn_is_even(rivel_param_target));
    rivel_print_int(rivel_fn_distance_from_limit(rivel_param_target, rivel_global_LIMIT));
    rivel_print_bool(rivel_fn_report_consistent(rivel_param_target, rivel_param_previous, rivel_param_total));
    int64_t rivel_return_value_0 = rivel_param_total;
    return rivel_return_value_0;
}

int main(void) {
    return rivel_exit_code(rivel_fn_main());
}
