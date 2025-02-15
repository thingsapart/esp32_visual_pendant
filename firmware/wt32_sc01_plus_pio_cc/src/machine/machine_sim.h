// duet_simulator.h
#ifndef DUET_SIMULATOR_H
#define DUET_SIMULATOR_H

#ifdef MACHINE_SIM

#include "machine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct duet_simulator_t duet_simulator_t;

struct duet_simulator_t {
    machine_interface_t base; // Inheritance-like structure
    // Add any Duet-specific state here
    float pos[3];             // Machine position
    bool axes_homed[3];
    int wcs;
    float feed_multiplier;
     float wcs_offsets[10][3]; // Simulate up to 10 WCS (G54-G59.3)
};

duet_simulator_t *duet_simulator_create(uint16_t sleep_ms);
void duet_simulator_destroy(duet_simulator_t *sim);

#ifdef __cplusplus
}
#endif

#endif // MACHINE_SIM

#endif // DUET_SIMULATOR_H