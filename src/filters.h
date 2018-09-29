#include <stdint.h>

struct lowpass_state {
    double a0;
    double b1;
    int16_t last_out;
};

int16_t lowpass(struct lowpass_state *state, int16_t sample);
void lowpass_init(struct lowpass_state *state, double x);
