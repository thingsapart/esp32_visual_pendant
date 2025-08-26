# Data Binding: `action` and `observes`

The data binding system provides a mechanism to connect the state of your application (the machine interface) to the UI, and to receive events from the UI back into your application logic. It is based on a "Model-View-ViewModel" (MVVM) pattern that creates a clean separation of concerns:

*   **View:** The UI definition in your YAML file.
*   **Model:** The `machine_interface_t` struct, representing the CNC machine's state.
*   **ViewModel:** The `data_binding` library, which mediates between the Model and the View.

This system is comprised of two main features, configured by the `action` and `observes` keys in your UI definition.

## `action`: UI to Machine Communication

The `action` key allows widgets to send commands to the machine. When a user interacts with a widget (e.g., clicks a button), a named action is dispatched to a handler that calls the corresponding function on the `machine_interface_t`.

### Action Reference

| Action Name | Description | Value Type | YAML Example |
| :--- | :--- | :--- | :--- |
| **Homing & Control** |
| `home_machine` | Homes all axes (`G28`). | `trigger` | `{ home_machine: trigger }` |
| `home_machine_x` | Homes the X axis (`G28 X`). | `trigger` | `{ home_machine_x: trigger }` |
| `home_machine_y` | Homes the Y axis (`G28 Y`). | `trigger` | `{ home_machine_y: trigger }` |
| `home_machine_z` | Homes the Z axis (`G28 Z`). | `trigger` | `{ home_machine_z: trigger }` |
| `feed_hold` | Toggles feed hold (`!`) and resume (`~`). | `trigger` | `{ feed_hold: trigger }` |
| `program_run` | Starts or resumes the current job (`M24`). | `trigger` | `{ program_run: trigger }` |
| `program_stop` | Issues a soft reset (`Ctrl+X`). | `trigger` | `{ program_stop: trigger }` |
| **Overrides** |
| `set_feed_override` | Sets the feed rate override percentage. | `float` | `{ set_feed_override: 110.0 }` |
| `set_speed_override` | Sets the spindle speed override percentage. | `float` | `{ set_speed_override: 95.0 }` |
| **Movement & Jogging** |
| `jog_continuous_start_x_plus` | Starts a continuous positive jog on the X axis. | `trigger` | `{ jog_continuous_start_x_plus: trigger }` |
| `jog_continuous_start_x_minus` | Starts a continuous negative jog on the X axis. | `trigger` | `{ jog_continuous_start_x_minus: trigger }` |
| ..._y_plus/minus | (Same for Y axis) | `trigger` | ... |
| ..._z_plus/minus | (Same for Z axis) | `trigger` | ... |
| `jog_continuous_stop` | Stops any continuous jog. | `trigger` | `{ jog_continuous_stop: trigger }` |
| `jog_step_x_plus` | Steps the X axis by `jog_step` distance. | `trigger` | `{ jog_step_x_plus: trigger }` |
| `jog_step_x_minus` | Steps the X axis by `-jog_step` distance. | `trigger` | `{ jog_step_x_minus: trigger }` |
| ..._y_plus/minus | (Same for Y axis) | `trigger` | ... |
| ..._z_plus/minus | (Same for Z axis) | `trigger` | ... |
| `cycle_jog_step` | Cycles through predefined jog step distances. | `cycle` | `{ cycle_jog_step: [0.01, 0.1, 1.0, 10.0] }` |
| `set_move_axis_x` | Sets the currently active axis for jogging to X. | `trigger` | `{ set_move_axis_x: trigger }` |
| `set_move_axis_y` | Sets the currently active axis for jogging to Y. | `trigger` | `{ set_move_axis_y: trigger }` |
| `set_move_axis_z` | Sets the currently active axis for jogging to Z. | `trigger` | `{ set_move_axis_z: trigger }` |
| `set_move_axis_off` | Deactivates axis jogging selection. | `trigger` | `{ set_move_axis_off: trigger }` |
| `cycle_move_axis` | Cycles the active jog axis (X -> Y -> Z -> OFF). | `trigger` | `{ cycle_move_axis: trigger }` |
| **Coordinate Systems (WCS)** |
| `set_wcs` | Sets the current Work Coordinate System. | `float` | `{ set_wcs: 2.0 }` (for G55) |
| `cycle_wcs` | Cycles to the next WCS (e.g., G54 -> G55). | `trigger` | `{ cycle_wcs: trigger }` |
| `zero_wcs_x` | Sets the current X position as the origin for the current WCS. | `trigger` | `{ zero_wcs_x: trigger }` |
| `zero_wcs_y` | Sets the current Y position as the origin for the current WCS. | `trigger` | `{ zero_wcs_y: trigger }` |
| `zero_wcs_z` | Sets the current Z position as the origin for the current WCS. | `trigger` | `{ zero_wcs_z: trigger }` |
| **Files & Macros** |
| `list_files` | Requests a file listing for a given path. | `string` | `{ list_files: "/gcodes" }` |
| `run_macro` | Runs a macro file by name. | `string` | `{ run_macro: "probe.g" }` |
| `start_job` | Starts a job file by name. | `string` | `{ start_job: "my_part.gcode" }` |
| **Probing** |
| `probe` | Executes a G-code probing routine. | `string` | `{ probe: "G38.2 Z-20 F100" }` |
| **Dialogs / Modals** |
| `dialog_ok` | Responds "OK" to the current modal dialog. | `trigger` | `{ dialog_ok: trigger }` |
| `dialog_cancel` | Responds "Cancel" to the current modal dialog. | `trigger` | `{ dialog_cancel: trigger }` |
| `dialog_choice` | Responds with a choice to a multi-choice dialog. | `float` | `{ dialog_choice: 1.0 }` (for 2nd choice) |
| `dialog_input_int` | Submits an integer value from an input dialog. | `float` | `{ dialog_input_int: 123.0 }` |
| `dialog_input_float`| Submits a float value from an input dialog. | `float` | `{ dialog_input_float: 45.6 }` |
| `dialog_input_str` | Submits a string value from an input dialog. | `string` | `{ dialog_input_str: "my_value" }` |
| **G-Code** |
| `send_gcode` | Sends a raw G-code string to the machine. | `string` | `{ send_gcode: "M3 S10000" }` |

## `observes`: Machine to UI Communication

The `observes` key allows widgets to automatically update their appearance in response to changes in the machine's state. You simply notify the data binding system when a piece of your data changes, and it handles updating the UI.

### Observable State Reference

| State Name | Description | Value Type | YAML Example |
| :--- | :--- | :--- | :--- |
| **Machine Status** |
| `is_connected` | `true` if a connection to the machine is active. | `bool` | `{ is_connected: { visible: { true: true, false: false } } }` |
| `machine_mode`| The current high-level status (e.g., "IDLE", "AUTO", "PAUSED").| `string` | `{ machine_mode: { text: "%s" } }` |
| `program_running` | `true` if a job is currently running. | `bool` | `{ program_running: { style: { true: '@style_active' } } }` |
| `program_paused` | `true` if a job is currently paused. | `bool` | `{ program_paused: { visible: true } }` |
| **Position** |
| `pos_x` / `_y` / `_z` | Machine-space X, Y, or Z position. | `float` | `{ pos_x: { text: "X: %.3f" } }` |
| `wcs_pos_x` / `_y` / `_z` | Work-space (WCS) X, Y, or Z position. | `float` | `{ wcs_pos_x: { text: "%.3f" } }` |
| `target_pos_x` / `_y` / `_z` | The final target position for the current move. | `float` | `{ target_pos_x: { text: "Target: %.3f" } }` |
| **Homing** |
| `x_is_homed` | `true` if the X axis has been homed. | `bool` | `{ x_is_homed: { style: { true: '@style_homed' } } }` |
| `y_is_homed` | `true` if the Y axis has been homed. | `bool` | `{ y_is_homed: { checked: { true: true, false: false } } }` |
| `z_is_homed` | `true` if the Z axis has been homed. | `bool` | `{ z_is_homed: { disabled: { true: false, false: true } } }` |
| **Coordinate Systems (WCS)** |
| `wcs_name` | The name of the active WCS (e.g., "G54", "G55"). | `string` | `{ wcs_name: { text: "%s" } }` |
| `wcs_number` | The number of the active WCS (1 for G54, etc.). | `float` | `{ wcs_number: { value: [LV_ANIM_OFF] } }` |
| `z_offset` | The current Z-offset value. | `float` | `{ z_offset: { text: "Z-Off: %.3f" } }` |
| **Feed & Speed** |
| `feed` | The current actual feed rate. | `float` | `{ feed: { text: "Feed: %.0f" } }` |
| `feed_requested` | The requested feed rate from G-code. | `float` | `{ feed_requested: { text: "Req: %.0f" } }` |
| `feed_override` | The current feed override percentage (e.g., 100.0 for 100%). | `float` | `{ feed_override: { value: [LV_ANIM_ON] } }` |
| **Spindle & Tool** |
| `spindle_0_rpm` | RPM of the first spindle. | `float` | `{ spindle_0_rpm: { text: "RPM: %.0f" } }` |
| `spindle_0_name` | Name of the first spindle. | `string` | `{ spindle_0_name: { text: "%s" } }` |
| `spindle_0_on` | `true` if the first spindle is active. | `bool` | `{ spindle_0_on: { style: { true: '@style_active' } } }` |
| `tool_name` | The name of the currently active tool. | `string` | `{ tool_name: { text: "Tool: %s" } }` |
| **Jogging & UI State** |
| `move_is_relative` | `true` if G-code moves are in relative mode (G91). | `bool` | `{ move_is_relative: { visible: true } }` |
| `move_is_step` | `true` if jogging is in step mode. | `bool` | `{ move_is_step: { style: { true: '@style_active' } } }` |
| `current_move_axis`| The currently selected jog axis ("X", "Y", "Z", or "OFF"). | `string` | `{ current_move_axis: { text: "Axis: %s" } }` |
| `jog_step` | The current step distance for jogging. | `float` | `{ jog_step: { text: "%.2f" } }` |
| **Sensors** |
| `probe_0_value` | The value of the first probe. | `float` | `{ probe_0_value: { text: "%.3f" } }` |
| `end_stop_0_triggered`| `true` if the first endstop is triggered. | `bool` | `{ end_stop_0_triggered: { visible: true } }` |
| **Files** |
| `filelist_gcodes`| A newline-separated list of g-code files. For `dropdown` widgets.| `string`| `{ filelist_gcodes: { text: "%s" } }`|
| `filelist_macros`| A newline-separated list of macro files. For `dropdown` widgets.| `string`| `{ filelist_macros: { text: "%s" } }`|
| **Dialogs / Modals** |
| `dialog_active` | `true` if a modal dialog is currently active. | `bool` | `{ dialog_active: { visible: true } }` |
| `dialog_title`| The title of the current dialog. | `string` | `{ dialog_title: { text: "%s" } }` |
| `dialog_text` | The main text/message of the current dialog. | `string` | `{ dialog_text: { text: "%s" } }` |
| `dialog_choices`| Newline-separated list of choices for a choice dialog. | `string` | `{ dialog_choices: { text: "%s" } }` |
| `dialog_mode` | The type of dialog (see `message_box_mode_t`). | `float` | `{ dialog_mode: { visible: { 2.0: true } } }` |

