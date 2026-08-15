/*
  my_plugin.c - VEVOR A7: physical "run job from SD card" button

  Polls BUTTON_RUN_PIN (default GPIO15, the unused "Probe" header; external
  pull-up on board, button shorts to GND). On a debounced press while the
  machine is idle it streams the first G-code file found in the SD card root.
  Homing before the job is enforced inside stream_file() (fs_stream.c patch).

  Part of grblHAL. Licensed under GPLv3 (same as parent project).
*/

#include "driver.h"

#include <string.h>

#include "grbl/hal.h"
#include "grbl/task.h"
#include "grbl/report.h"
#include "grbl/state_machine.h"
#include "grbl/vfs.h"
#include "sdcard/fs_stream.h"

#ifndef BUTTON_RUN_PIN
#define BUTTON_RUN_PIN GPIO_NUM_15
#endif

#define BUTTON_POLL_MS      20
#define BUTTON_DEBOUNCE     3    // consecutive polls (60 ms) to accept a state

static on_report_options_ptr on_report_options;
static on_user_command_ptr on_user_command;
static uint8_t pressed_count = 0, released_count = 0;
static bool armed = false; // becomes true only after a stable released state

static bool find_first_gcode (char *path, size_t len)
{
    static const char *exts[] = { ".nc", ".gc", ".gcode", ".tap", ".ngc", ".txt" };

    bool found = false;
    vfs_dir_t *dir = vfs_opendir("/");
    vfs_dirent_t *dirent;

    if(dir == NULL)
        return false;

    while(!found && (dirent = vfs_readdir(dir)) != NULL) {

        if(dirent->st_mode.directory || dirent->name[0] == '.' || dirent->name[0] == '\0')
            continue;

        char *ext = strrchr(dirent->name, '.');
        if(ext) for(uint32_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
            if(strcasecmp(ext, exts[i]) == 0) {
                *path = '/';
                strncpy(path + 1, dirent->name, len - 2);
                path[len - 1] = '\0';
                found = true;
                break;
            }
        }
    }

    vfs_closedir(dir);

    return found;
}

static void run_first_file (void *data)
{
    char path[64];

    if(state_get() != STATE_IDLE)
        return;

    if(find_first_gcode(path, sizeof(path))) {
        report_message("Run button: starting job", Message_Info);
        if(stream_file(state_get(), path) != Status_OK)
            report_message("Run button: failed to start file", Message_Warning);
    } else
        report_message("Run button: no G-code file on card", Message_Warning);
}

static void button_poll (void *data)
{
    task_add_delayed(button_poll, NULL, BUTTON_POLL_MS);

    bool pressed = gpio_get_level(BUTTON_RUN_PIN) == 0; // active low

    if(pressed) {
        released_count = 0;
        if(pressed_count < BUTTON_DEBOUNCE && ++pressed_count == BUTTON_DEBOUNCE && armed) {
            armed = false;
            task_add_immediate(run_first_file, NULL);
        }
    } else {
        pressed_count = 0;
        if(released_count < BUTTON_DEBOUNCE && ++released_count == BUTTON_DEBOUNCE)
            armed = true;
    }
}

// Handles commands arriving over HTTP (/command?cmd=...) where the normal
// input path is unavailable because the websocket service is disabled:
//   RUNSD=<path> - stream a file from the mounted SD card (auto-homes first)
//   RUNSD        - stream the first G-code file found in the card root
// NOTE: the HTTP handler runs in the httpd task - motion (homing) must not be
// started from there, so the job is queued for the protocol (foreground) task.
static char http_run_path[64];

static void http_run_task (void *data)
{
    if(state_get() != STATE_IDLE)
        report_message("HTTP run: machine not idle", Message_Warning);
    else if(*http_run_path == '\0' && !find_first_gcode(http_run_path, sizeof(http_run_path)))
        report_message("HTTP run: no G-code file on card", Message_Warning);
    else {
        report_message("HTTP run: starting job", Message_Info);
        if(stream_file(state_get(), http_run_path) != Status_OK)
            report_message("HTTP run: failed to start file", Message_Warning);
    }
}

static status_code_t user_command (char *line)
{
    if(!strncmp(line, "RUNSD", 5)) {

        if(state_get() != STATE_IDLE)
            return Status_IdleError;

        if(line[5] == '=' && line[6] != '\0') {
            if(line[6] != '/') {
                *http_run_path = '/';
                strncpy(http_run_path + 1, line + 6, sizeof(http_run_path) - 2);
            } else
                strncpy(http_run_path, line + 6, sizeof(http_run_path) - 1);
            http_run_path[sizeof(http_run_path) - 1] = '\0';
        } else
            *http_run_path = '\0'; // resolved to the first file in the foreground task

        task_add_immediate(http_run_task, NULL);

        // The HTTP handler serves /ram/qry.txt after a handled command - write it
        // so the client gets a 200 "ok" instead of a 404.
        vfs_file_t *response;
        if((response = vfs_open("/ram/qry", "w"))) {
            vfs_puts("ok\n", response);
            vfs_close(response);
        }

        return Status_OK;
    }

    return on_user_command == NULL ? Status_Unhandled : on_user_command(line);
}

static void report_my_options (bool newopt)
{
    on_report_options(newopt);

    if(!newopt)
        report_plugin("VEVOR A7 run button", "1.0");
}

void my_plugin_init (void)
{
    gpio_reset_pin(BUTTON_RUN_PIN);
    gpio_set_direction(BUTTON_RUN_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_RUN_PIN); // belt and braces; board has an external pull-up

    on_report_options = grbl.on_report_options;
    grbl.on_report_options = report_my_options;

    on_user_command = grbl.on_user_command;
    grbl.on_user_command = user_command;

    task_add_delayed(button_poll, NULL, 500); // let boot settle before first poll
}
