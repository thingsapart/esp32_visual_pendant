// multi_machine_interface.h
#ifndef MULTI_MACHINE_INTERFACE_H
#define MULTI_MACHINE_INTERFACE_H

#include "machine_interface.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MACHINES 5 // Maximum number of machines to support.  Adjust as needed.

typedef struct {
    machine_interface_t base;
    machine_interface_t *machines[MAX_MACHINES];
    size_t num_machines;
     bool connected; // keep the multi-machine connected if any of its delegates are.
} multi_machine_interface_t;

multi_machine_interface_t *multi_machine_interface_create();
multi_machine_interface_t *multi_machine_interface_init(multi_machine_interface_t *self);
void multi_machine_interface_destroy(multi_machine_interface_t *self);
void multi_machine_interface_deinit(multi_machine_interface_t *self);

bool multi_machine_add_impl(multi_machine_interface_t *self, machine_interface_t *machine);

#ifdef __cplusplus
}
#endif

#endif // MULTI_MACHINE_INTERFACE_H