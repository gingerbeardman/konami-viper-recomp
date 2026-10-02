#include "controller_math.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(controller_axis(-32768, .1, 1.5) == -1);
    assert(controller_axis(32767, .1, 1.5) == 1);
    assert(controller_axis(0, .1, 1.5) == 0);
    assert(controller_axis(3276, .1, 1.5) == 0);
    assert(controller_axis(-3276, .1, 1.5) == 0);
    assert(controller_axis(3277, .1, 1.5) < .00001);
    assert(controller_axis(16384, .1, 1.5) < controller_axis(16384, .1, 1));
    double previous = -1;
    for (int raw = -32768; raw <= 32767; raw++) {
        double v = controller_axis(raw, .1, 1.5);
        assert(v >= previous && v >= -1 && v <= 1);
        previous = v;
    }
    assert(controller_trigger(-32768, .03) == 0);
    assert(controller_trigger(0, .03) == 0);
    assert(controller_trigger(983, .03) == 0);
    assert(controller_trigger(984, .03) < .0001);
    assert(controller_trigger(32767, .03) == 1);
    puts("controller transfer functions: passed");
}
