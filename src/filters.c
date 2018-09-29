#include "filters.h"

int16_t lowpass(struct lowpass_state *state, int16_t sample)
{
    return state->last_out = (state->a0 * sample) + (state->b1 * state->last_out);
}

void lowpass_init(struct lowpass_state *state, double x /* between 0 and 1 */) {
    state->a0 = 1 - x;
    state->b1 = x;
}
