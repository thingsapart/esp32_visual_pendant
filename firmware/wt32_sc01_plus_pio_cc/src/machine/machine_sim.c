#ifdef MACHINE_SIM

// duet_simulator.c
#include "machine_sim.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>  // For fabs()

#include "debug.h"

// --- Helper Functions ---

static void trim_string(char *str) {
    if (!str) return;

    char *start = str;
    while (isspace((unsigned char)*start)) start++;

    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    *(end + 1) = '\0';

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}


static void parse_gcode_line(duet_simulator_t *sim, const char *gcode_line) {
    char line[128]; // Buffer for processing the line
    strncpy(line, gcode_line, sizeof(line) -1);
    line[sizeof(line) - 1] = '\0'; // Ensure null termination.
    trim_string(line);

    if (strlen(line) == 0) return;


    char *command = strtok(line, " ");
    if (!command) return;


    if (strcmp(command, "G28") == 0) {
         // Simple homing simulation:  set all axes to homed and position to 0.
        for (int i = 0; i < 3; i++) {
            sim->base.axes_homed[i] = true;
            sim->base.position[i] = 0.0f;
             sim->base.wcs_position[i] =  sim->wcs_offsets[sim->base.wcs][i];
        }
        machine_interface_home_updated(&sim->base);
        machine_interface_position_updated(&sim->base);

    } else if (strcmp(command, "G53") == 0) {
        sim->base.wcs = 0; // Machine Coordinate System
         machine_interface_wcs_updated(&sim->base);
    }  else if (strncmp(command, "G54", 3) == 0) {
        sim->base.wcs = 1;
        machine_interface_wcs_updated(&sim->base);
    } else if (strncmp(command, "G55", 3) == 0) {
        sim->base.wcs = 2;
        machine_interface_wcs_updated(&sim->base);
    } else if (strncmp(command, "G56", 3) == 0) {
        sim->base.wcs = 3;
        machine_interface_wcs_updated(&sim->base);
    } else if (strncmp(command, "G57", 3) == 0) {
       sim->base.wcs = 4;
       machine_interface_wcs_updated(&sim->base);
    } else if (strncmp(command, "G58", 3) == 0) {
       sim->base.wcs = 5;
       machine_interface_wcs_updated(&sim->base);
    } else if (strncmp(command, "G59", 3) == 0) {
        // Check for G59, G59.1, G59.2, G59.3
        if (strlen(command) == 3) {
            sim->base.wcs = 6; // G59
            machine_interface_wcs_updated(&sim->base);
        } else if (strcmp(command, "G59.1") == 0) {
            sim->base.wcs = 7;
            machine_interface_wcs_updated(&sim->base);
        } else if (strcmp(command, "G59.2") == 0) {
           sim->base.wcs = 8;
           machine_interface_wcs_updated(&sim->base);
        } else if (strcmp(command, "G59.3") == 0) {
          sim->base.wcs = 9;
          machine_interface_wcs_updated(&sim->base);
        }
    } else if (strcmp(command, "G10") == 0) {
      // G10 L20 Px Xx Yy Zz  (Set WCS offsets)
        char *p_arg = NULL;
        char *l_arg = NULL;
        int wcs_index = -1;
         float offsets[3] = {0.0f, 0.0f, 0.0f}; // Store X, Y, Z offsets

        char *token = strtok(NULL, " ");
        while (token != NULL) {
            if (token[0] == 'P') {
                p_arg = token + 1;
                wcs_index = atoi(p_arg) -1; //Duet WCS are 1-indexed, array is 0.
                if (wcs_index < 0 || wcs_index > 9) {
                    // Invalid WCS index
                    printf("Invalid WCS index in G10: %s\n", p_arg);
                    return;
                }

            } else if (token[0] == 'L') {
                l_arg = token + 1;
                if(strcmp(l_arg, "20") != 0)
                {
                    printf("only L20 is supported in G10");
                    return;
                }

            } else if (token[0] == 'X') {
               offsets[0] = atof(token + 1);
            } else if (token[0] == 'Y') {
                offsets[1] = atof(token + 1);
            } else if (token[0] == 'Z') {
                 offsets[2] = atof(token + 1);
            }
            // TODO: handle other axes if needed.

            token = strtok(NULL, " ");
        }

        if (wcs_index != -1)
        {
             // apply the offset relative to the current position
            for (int i = 0; i < 3; i++) {
                  sim->wcs_offsets[wcs_index][i] = offsets[i] - sim->base.position[i];
            }
           
            printf("Setting WCS offsets for index %d: X=%.3f, Y=%.3f, Z=%.3f\n", wcs_index, sim->wcs_offsets[wcs_index][0], sim->wcs_offsets[wcs_index][1], sim->wcs_offsets[wcs_index][2]);
             machine_interface_position_updated(&sim->base); // WCS Offsets affect reported pos.
        }

    }  else if (strcmp(command, "G1") == 0 || strcmp(command, "G0") == 0) {
        // Very simplified movement simulation.
        char *token = strtok(NULL, " "); // Start parsing arguments from the rest of the line
         while (token != NULL) {
            if (token[0] == 'X') {
                sim->base.position[0] = atof(token + 1);
            } else if (token[0] == 'Y') {
               sim->base.position[1] = atof(token + 1);
            } else if (token[0] == 'Z') {
               sim->base.position[2] = atof(token + 1);
            } // TODO: handle other axes/args.
             token = strtok(NULL, " ");
        }

       machine_interface_position_updated(&sim->base);
    } // TODO: Add more G-code parsing as needed for your simulation.
}

static void duet_send_gcode(machine_interface_t *self, const char *gcode, uint32_t poll_state) {
    _df(0, "SENDING GCODE: %s\n", gcode);

    // In a real implementation, you'd send the G-code to the Duet via UART.
    // Here, we'll just parse it and update the simulator's state.
    duet_simulator_t *sim = (duet_simulator_t *)self;  // Cast to the derived type
     // Split the G-code into individual lines
    char *gcode_copy = strdup(gcode); // Create a copy since strtok modifies the string
    if (!gcode_copy) {
        LV_LOG_ERROR("Failed to allocate memory for gcode copy");
        return;
    }

    char *line = strtok(gcode_copy, "\n");
    while (line != NULL) {
        parse_gcode_line(sim, line);
        line = strtok(NULL, "\n");
    }

    free(gcode_copy);

    // Now, "respond" based on poll_state (if needed for this gcode)
     self->poll_state = poll_state;
    machine_interface_process_gcode_q(self); // Process any resulting responses
}
static void duet__send_gcode(machine_interface_t *self, const char *gcode)
{
    duet_send_gcode(self, gcode, self->poll_state);
}

static bool duet_is_connected(machine_interface_t *self) {
    // For the simulator, we'll just assume it's always connected.
    return true;
}

// Simplified file listing for simulation
static const char *simulated_gcodes_files[] = {
    "Updown.gcode", "Updown1.gcode", "updown 6.1.gcode", "MiniNC Z Plate.gcode",
    "updown 6.1 5mm-adaptive.gcode", "updown 6.1 - adaptive 5mm, pocket 0.5mm-0.75mm, slot 0.5mm.gcode",
    "MiniNC Z Plate Contout Only.gcode", NULL // Null-terminated
};

static const char *simulated_macros_files[] = {
    "probe_work.g", "free_z.g", "all_zero.g", "z_zero.g", "zero_workspace.g",
    "work_align_xy10mm.gcode", "move_free.g", "touch_probe_work.g", NULL // Null-terminated
};

static void duet_list_files(machine_interface_t *self, const char *path)
{
    duet_simulator_t *sim = (duet_simulator_t *)self;
    if (strcmp(path, "/macros") == 0) {
       sim->base.filelists[1].fdir = path;
       sim->base.filelists[1].files = simulated_macros_files;

    } else if (strcmp(path, "/gcodes") == 0) {
       sim->base.filelists[0].fdir = path;
       sim->base.filelists[0].files = simulated_gcodes_files;

    } else {
        // Unknown path, handle as error (empty list, for example)
        LV_LOG_WARN("list_files:  unknown path: %s", path);
         sim->base.filelists[0].fdir = NULL;
         sim->base.filelists[0].files = NULL;
         sim->base.filelists[1].fdir = NULL;
         sim->base.filelists[1].files = NULL;
        return;
    }

    machine_interface_files_updated(self, path);

}

static void duet_run_macro(machine_interface_t *self, const char *macro_name)
{
   // In a real implementation, you might send a command to the Duet to run the macro.
   // For the simulator, you *could* simulate some macro behavior, but for simplicity,
   // we'll just log it.
   printf("Running macro: %s\n", macro_name);
   // You might also update the machine_status here (e.g., to BUSY).
}

static void duet_start_job(machine_interface_t *self, const char *job_name) {
   // Similar to run_macro, you'd send a command to the Duet.  For simulation,
   // you could simulate loading a file, setting job status, etc.
   printf("Starting job: %s\n", job_name);
   // You might also update the machine_status here (e.g., to RUNNING).
}


static void duet_move_to(machine_interface_t *self, const char axis, float feed, float value, bool relative)
{
    // Construct a G-code string and send it
    char gcode[64];
    if (relative)
    {
        snprintf(gcode, sizeof(gcode), "G91\nG1 %c%.3f F%.3f\nG90", axis, value, feed);
    } else {
        snprintf(gcode, sizeof(gcode), "G90\nG1 %c%.3f F%.3f", axis, value, feed);
    }
    duet_send_gcode(self, gcode, 0); // 0 for non-polling update.
}

static void duet_move_continuous(machine_interface_t *self, const char axis, float feed, int direction)
{
    // Construct a G-code string and send it
    char gcode[64];
    if (direction == 0)
    {
        snprintf(gcode, sizeof(gcode), "M120\nG91\nG1 %c0 F%.3f\nG90\nM121", axis, feed);

    } else if (direction > 0) {
        snprintf(gcode, sizeof(gcode), "G91\nG1 %c1000 F%.3f\nG90", axis, feed); // + direction
    } else {
         snprintf(gcode, sizeof(gcode), "G91\nG1 %c-1000 F%.3f\nG90", axis, feed); // - direction
    }
    duet_send_gcode(self, gcode, 0); // 0 for non-polling update.
}

static void duet_move_continuous_stop(machine_interface_t *self) {
   // Duet stops continuous moves on any other movement command.  A simple G4 P0 (dwell for 0ms)
   // will stop the continuous move without causing any actual movement.
   duet_send_gcode(self, "G4 P0", 0);
}

static void duet__continuous_move(machine_interface_t *self, const char axis, float feed, int direction) {
   if (direction == 0)
   {
      self->moving_target_position[0] = NAN;
      self->moving_target_position[1] = NAN;
      self->moving_target_position[2] = NAN;
   }
    else
    {
        int axis_idx = machine_interface_axis_idx(self, axis);
        if (axis_idx >= 0)
        {
            self->moving_target_position[axis_idx] = direction > 0 ? 1000.0f : -1000.0f;
        }

    }
}

static void duet__continuous_stop(machine_interface_t *self)
{
    self->moving_target_position[0] = NAN;
    self->moving_target_position[1] = NAN;
    self->moving_target_position[2] = NAN;
}

static void duet_move(machine_interface_t *self, const char axis, float feed, float value) {
   duet_move_to(self, axis, feed, value, self->move_relative);
}

static void duet_home_all(machine_interface_t *self)
{
    duet_send_gcode(self, "G28", 0);
}

static void duet_home(machine_interface_t *self, const char *axes)
{
    char gcode[64];
    snprintf(gcode, sizeof(gcode), "G28 %s", axes);
    duet_send_gcode(self, gcode, 0);
}

static void duet_set_wcs(machine_interface_t *self, int wcs)
{
    char gcode[16];
    if (wcs >= 1 && wcs <= 6) { // G54 - G59
        snprintf(gcode, sizeof(gcode), "G%d", 53 + wcs);
    } else if (wcs >= 7 && wcs <= 9) { // G59.1 - G59.3
        snprintf(gcode, sizeof(gcode), "G59.%d", wcs - 6); // G59.1 == 7
    }
     else {
        // Invalid WCS
        LV_LOG_WARN("set_wcs:  invalid WCS %d", wcs);
        return;
    }
    duet_send_gcode(self, gcode, 0);
}

static void duet_set_wcs_zero(machine_interface_t *self, int wcs, const char *axes) {
    char gcode[64];
    if (wcs >= 1 && wcs <= 6) { // G54 - G59
        snprintf(gcode, sizeof(gcode), "G10 L20 P%d %s", wcs, axes);
    } else if (wcs >= 7 && wcs <= 9) { // G59.1 - G59.3
         snprintf(gcode, sizeof(gcode), "G10 L20 P%d %s", wcs, axes);
    } else {
        // Invalid WCS
        printf("Invalid WCS: %d\n", wcs);
        return;
    }
    duet_send_gcode(self, gcode, 0); // No polling update needed
}

static void duet_next_wcs(machine_interface_t *self)
{
    int next_wcs = self->wcs + 1;
    if (next_wcs > 9) {
        next_wcs = 1; // Wrap around to G54
    }
    duet_set_wcs(self, next_wcs);
}

static char* duet_debug_print(machine_interface_t *self)
{
    duet_simulator_t *sim = (duet_simulator_t*) self;
    // Create a buffer for the debug string.
    #define DEBUG_BUFFER_SIZE 512
    char *buffer = (char *) malloc(sizeof(char) * 512);

    snprintf(buffer, DEBUG_BUFFER_SIZE,
        "Duet Simulator:\n"
        "  Position: X=%.3f, Y=%.3f, Z=%.3f\n"
        "  WCS Position: X=%.3f, Y=%.3f, Z=%.3f\n"
        "  Axes Homed: X=%s, Y=%s, Z=%s\n"
        "  WCS: %s\n"
        "  Feed Multiplier: %.2f\n"
        ,
        self->position[0], self->position[1], self->position[2],
        self->wcs_position[0], self->wcs_position[1], self->wcs_position[2],
        self->axes_homed[0] ? "true" : "false", self->axes_homed[1] ? "true" : "false", sim->base.axes_homed[2] ? "true" : "false",
        machine_interface_get_wcs_str(self, self->wcs),
        self->feed_multiplier
    );
    return buffer;

}

static void duet_update_machine_state(machine_interface_t *mach, uint32_t poll_state) {
    duet_simulator_t *self = (duet_simulator_t *) mach;
    // In a real implementation, you'd query the Duet for its state.
    // Here, we'll simulate some responses.
    // duet_simulator_t *sim = (duet_simulator_t *)self;
     char response[512]; // Buffer for responses

     if (poll_state & MACHINE_POSITION) {
         // Simulate response to M409 K"move.axes"
          float x = self->base.position[0];
          float xwcs = self->wcs_offsets[self->base.wcs][0];
          const char *xh = self->base.axes_homed[0] ? "true" : "false";
          float xu = x + xwcs;
          float y = self->base.position[1];
          const char *yh = self->base.axes_homed[1] ? "true" : "false";
          float ywcs = self->wcs_offsets[self->base.wcs][1];
          float yu = y + ywcs;
          float z = self->base.position[2];
          const char *zh = self->base.axes_homed[2] ? "true" : "false";
          float zwcs = self->wcs_offsets[self->base.wcs][2];
          float zu = z + zwcs;


         snprintf(response, sizeof(response),
             "{\"key\":\"move.axes\",\"flags\":\"\",\"result\":[{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.2\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"X\",\"machinePosition\":%.3f,\"max\":200.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.1\"],\"homed\":%s,\"jerk\":300.0,\"letter\":\"Y\",\"machinePosition\":%.3f,\"max\":160.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]},{\"acceleration\":100.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.3\"],\"homed\":%s,\"jerk\":30.0,\"letter\":\"Z\",\"machinePosition\":%.3f,\"max\":70.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":-1.00,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":100.0,\"speed\":1000.0,\"stepsPerMm\":400.00,\"userPosition\":%.3f,\"visible\":true,\"workplaceOffsets\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}],\"next\":0}\n",
             xh, x, xu, self->wcs_offsets[0][0],self->wcs_offsets[1][0],self->wcs_offsets[2][0],self->wcs_offsets[3][0],self->wcs_offsets[4][0],self->wcs_offsets[5][0],self->wcs_offsets[6][0],self->wcs_offsets[7][0],self->wcs_offsets[8][0],self->wcs_offsets[9][0],
             yh, y, yu, self->wcs_offsets[0][1],self->wcs_offsets[1][1],self->wcs_offsets[2][1],self->wcs_offsets[3][1],self->wcs_offsets[4][1],self->wcs_offsets[5][1],self->wcs_offsets[6][1],self->wcs_offsets[7][1],self->wcs_offsets[8][1],self->wcs_offsets[9][1],
             zh, z, zu, self->wcs_offsets[0][2],self->wcs_offsets[1][2],self->wcs_offsets[2][2],self->wcs_offsets[3][2],self->wcs_offsets[4][2],self->wcs_offsets[5][2],self->wcs_offsets[6][2],self->wcs_offsets[7][2],self->wcs_offsets[8][2],self->wcs_offsets[9][2]
             );

         // Simulate pushing the response to a queue (you'd handle this in a real UART implementation)
         gcode_queue_push(&self->base.gcode_queue, response);
    }


    if (poll_state & (LIST_FILES | LIST_MACROS)) //"M20"
    {
        char pathbuf[64] = {0};
        const char ** file_list = NULL;

        if (poll_state & LIST_FILES)
        {
             strncpy(pathbuf, self->base.filelists[0].fdir, sizeof(pathbuf)-1);
             file_list = self->base.filelists[0].files;

        } else if (poll_state & LIST_MACROS) {
            strncpy(pathbuf, self->base.filelists[1].fdir, sizeof(pathbuf)-1);
            file_list = self->base.filelists[1].files;
        }

         if (strlen(pathbuf) > 0 && file_list != NULL)
         {
             // count files:
             int num_files = 0;
             while (file_list[num_files] != NULL) {
                 num_files++;
             }
             // Build the file list string
             char files_str[512] = {0}; // Adjust size as needed
             for (int i = 0; file_list[i] != NULL; i++) {
                 strncat(files_str, "\"", sizeof(files_str) - strlen(files_str) - 1);
                 strncat(files_str, file_list[i], sizeof(files_str) - strlen(files_str) - 1);
                 strncat(files_str, "\"", sizeof(files_str) - strlen(files_str) - 1);
                 if (i < num_files - 1) {
                     strncat(files_str, ",", sizeof(files_str) - strlen(files_str) - 1);
                 }
             }

              snprintf(response, sizeof(response),
                  "{\"dir\":\"%s\",\"first\":0,\"files\":[%s],\"next\":0,\"err\":0}\n",
                  pathbuf, files_str);

                gcode_queue_push(&self->base.gcode_queue, response);
         } else {
            // error:
             snprintf(response, sizeof(response),
                  "{\"dir\":\"%s\",\"err\":2}\n", // err=2 is a common error code.
                  pathbuf);
            gcode_queue_push(&self->base.gcode_queue, response);
         }
    }

     if (poll_state & MACHINE_POSITION_EXT)
     {
        if (strcmp(self->base.gcode_queue.buffer[self->base.gcode_queue.tail], "{\"key\":\"move.axes\",\"flags\":\"\",\"result\":[{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.2\"],\"homed\":true,\"jerk\":300.0,\"letter\":\"X\",\"machinePosition\":0.000,\"max\":200.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":0.000,\"visible\":true,\"workplaceOffsets\":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]},{\"acceleration\":900.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.1\"],\"homed\":true,\"jerk\":300.0,\"letter\":\"Y\",\"machinePosition\":0.000,\"max\":160.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":0,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":900.0,\"speed\":5000.0,\"stepsPerMm\":800.00,\"userPosition\":0.000,\"visible\":true,\"workplaceOffsets\":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]},{\"acceleration\":100.0,\"babystep\":0,\"backlash\":0,\"current\":1450,\"drivers\":[\"0.3\"],\"homed\":true,\"jerk\":30.0,\"letter\":\"Z\",\"machinePosition\":0.000,\"max\":70.00,\"maxProbed\":false,\"microstepping\":{\"interpolated\":true,\"value\":16},\"min\":-1.00,\"minProbed\":false,\"percentCurrent\":100,\"percentStstCurrent\":100,\"reducedAcceleration\":100.0,\"speed\":1000.0,\"stepsPerMm\":400.00,\"userPosition\":0.000,\"visible\":true,\"workplaceOffsets\":[0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000]}],\"next\":0}\n") != 0)
        {
            // avoid repeat answer to move.axes.
            // TODO: implement.
        }

        // network.
         snprintf(response, sizeof(response), "{\"key\":\"network\",\"flags\":\"\",\"result\":{\"corsSite\":\"\",\"hostname\":\"mininc\",\"interfaces\":[{\"actualIP\":\"0.0.0.0\",\"firmwareVersion\":\"(unknown)\",\"gateway\":\"0.0.0.0\",\"state\":\"disabled\",\"subnet\":\"255.255.255.0\",\"type\":\"wifi\"}],\"name\":\"MiniNC\"}}\n");
         gcode_queue_push(&self->base.gcode_queue, response);

        // move.currentMove
         snprintf(response, sizeof(response), "{\"key\":\"move.currentMove\",\"flags\":\"\",\"result\":{\"acceleration\":0,\"deceleration\":0,\"extrusionRate\":0,\"requestedSpeed\":%d,\"topSpeed\":%d}}\n", (int)self->base.feed_multiplier, 1000);
         gcode_queue_push(&self->base.gcode_queue, response);

         //'move.speedFactor'
         snprintf(response, sizeof(response), "{\"key\":\"move.speedFactor\",\"flags\":\"\",\"result\":%f}\n", self->base.feed_multiplier);
         gcode_queue_push(&self->base.gcode_queue, response);

         snprintf(response, sizeof(response), "{\"key\":\"job\",\"flags\":\"d3\",\"result\":{\"file\":{\"filament\":[],\"height\":0,\"layerHeight\":0,\"numLayers\":0,\"size\":0,\"thumbnails\":[]},\"filePosition\":0,\"lastDuration\":0,\"lastWarmUpDuration\":0,\"timesLeft\":{}}}\n");
         gcode_queue_push(&self->base.gcode_queue, response);

         snprintf(response, sizeof(response), "{\"key\":\"global\",\"flags\":\"\",\"result\":{\"varsLoaded\":true,\"parkZ\":2}}\n");
         gcode_queue_push(&self->base.gcode_queue, response);

         snprintf(response, sizeof(response),  "{\"key\":\"state.messageBox\",\"flags\":\"\",\"result\":null}\n");
          gcode_queue_push(&self->base.gcode_queue, response);

          snprintf(response, sizeof(response), "{\"key\":\"sensors.endstops[]\",\"flags\":\"\",\"result\":[null,null,null],\"next\":0}\n");
          gcode_queue_push(&self->base.gcode_queue, response);

        snprintf(response, sizeof(response),  "{\"key\":\"move.workplaceNumber\",\"flags\":\"\",\"result\":%d}\n", self->base.wcs);
          gcode_queue_push(&self->base.gcode_queue, response);

          snprintf(response, sizeof(response), "{\"key\":\"sensors.probes[].values[]\",\"flags\":\"\",\"result\":[0, 10],\"next\":0}\n");
          gcode_queue_push(&self->base.gcode_queue, response);

          snprintf(response, sizeof(response), "{\"key\":\"\",\"result\":null}\n");
          gcode_queue_push(&self->base.gcode_queue, response);
     }

    // Add other state updates as needed (spindle speed, tool status, etc.)
}


duet_simulator_t *duet_simulator_create(uint16_t sleep_ms) {
    duet_simulator_t *sim = (duet_simulator_t *)malloc(sizeof(duet_simulator_t));
    if (!sim) {
         LV_LOG_ERROR("duet_sim alloc failed!");
        return NULL;
    }
    //Crucially, *initialize* the base class using your init function.  Do this *before* setting your vtable entries.
    machine_interface_init(&sim->base, sleep_ms);

    // Initialize Duet-specific state
    for (int i = 0; i < 3; i++) {
        sim->base.position[i] = 0.0f;
        sim->base.axes_homed[i] = false;
         for (int j = 0; j < 10; j++)
         {
            sim->wcs_offsets[j][i] = 0.0f; // Initialize all WCS offsets to 0
         }
    }
    sim->base.wcs = 0;  // Machine coordinates
    sim->base.feed_multiplier = 1.0f;


    // Override the virtual methods with Duet-specific implementations
    sim->base.send_gcode = duet_send_gcode;
    sim->base._send_gcode = duet__send_gcode;
    sim->base.is_connected = duet_is_connected;
    sim->base.list_files = duet_list_files;
    sim->base.run_macro = duet_run_macro;
    sim->base.start_job = duet_start_job;
    sim->base._move_to = duet_move_to;
    sim->base.move_continuous = duet_move_continuous;
    sim->base.move_continuous_stop = duet_move_continuous_stop;
    sim->base.move = duet_move;
    sim->base.home_all = duet_home_all;
    sim->base.home = duet_home;
    sim->base.set_wcs = duet_set_wcs;
    sim->base.set_wcs_zero = duet_set_wcs_zero;
    sim->base.next_wcs = duet_next_wcs;
    sim->base._update_machine_state = duet_update_machine_state;
    sim->base.debug_print = duet_debug_print;
    sim->base._continuous_move = duet__continuous_move;
    sim->base._continuous_stop = duet__continuous_stop;

    return sim;
}

void duet_simulator_destroy(duet_simulator_t *sim) {
    if (!sim) return;

    // Free any Duet-specific resources here

    // Call the base class deinit function to handle common cleanup
    machine_interface_deinit(&sim->base);

    free(sim);
}

#endif