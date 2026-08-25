#include <stdint.h>

#include "amy_prng.h"

int32_t host_vector_amy_prng_repeatable(void)
{
    uint64_t first = amy_prng_initial_state(AMY_PRNG_DEFAULT_SEED, 12U);
    uint64_t second = amy_prng_initial_state(AMY_PRNG_DEFAULT_SEED, 12U);
    int32_t equal = amy_prng_next_i32(&first) == amy_prng_next_i32(&second);
    equal &= amy_prng_next_i32(&first) == amy_prng_next_i32(&second);
    equal &= amy_prng_next_i32(&first) == amy_prng_next_i32(&second);
    return equal && first == second;
}

int32_t host_vector_amy_prng_streams_are_distinct(void)
{
    uint64_t first = amy_prng_initial_state(AMY_PRNG_DEFAULT_SEED, 12U);
    uint64_t second = amy_prng_initial_state(AMY_PRNG_DEFAULT_SEED, 76U);
    return first != second && amy_prng_next_i32(&first) != amy_prng_next_i32(&second);
}

int32_t host_vector_amy_prng_order_independent(void)
{
    uint64_t first_ab = amy_prng_initial_state(AMY_PRNG_DEFAULT_SEED, 12U);
    uint64_t second_ab = amy_prng_initial_state(AMY_PRNG_DEFAULT_SEED, 76U);
    uint64_t first_ba = first_ab;
    uint64_t second_ba = second_ab;
    int32_t output_first_ab = amy_prng_next_i32(&first_ab);
    int32_t output_second_ab = amy_prng_next_i32(&second_ab);
    int32_t output_second_ba = amy_prng_next_i32(&second_ba);
    int32_t output_first_ba = amy_prng_next_i32(&first_ba);
    return output_first_ab == output_first_ba && output_second_ab == output_second_ba &&
           first_ab == first_ba && second_ab == second_ba;
}
