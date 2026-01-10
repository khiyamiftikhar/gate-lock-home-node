#include "ui_home.h"
#include "ui_defs.h"

// Screen structure (auto-generated)
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "ui_worker.h"

// ------------------------------
// UI SCREEN STRUCTURE
// ------------------------------
// ------------------------------
// UI SCREEN STRUCTURE
// ------------------------------
ui_screen_t home_screen = {
    .name = "Home_Screen",
    .child_count = 3,
    .children = {
        {
            .type = UI_CHILD_LABEL,
            .id = "wifi_ssid",
            .lv_obj = NULL,
            .x = 57, .y = 0,
            .w = 46, .h = 26,
            .icon = NULL,
            .current_state = 0,
            .text = "",
            .initial_value = 0
        },
        {
            .type = UI_CHILD_LABEL,
            .id = "discovery_msg",
            .lv_obj = NULL,
            .x = 0, .y = 1,
            .w = 52, .h = 26,
            .icon = NULL,
            .current_state = 0,
            .text = "",
            .initial_value = 0
        },
        {
            .type = UI_CHILD_LABEL,
            .id = "main_label",
            .lv_obj = NULL,
            .x = 2, .y = 31,
            .w = 122, .h = 28,
            .icon = NULL,
            .current_state = 0,
            .text = "",
            .initial_value = 0
        },
    },
    .lv_screen = NULL
};


// ------------------------------
// UI JOB DATA STRUCTS
// ------------------------------

typedef struct {
    uint8_t child_index;
    char text[UI_MAX_STRING_LENGTH];
} ui_home_screen_label_job_t;


// ------------------------------
// UI JOB CALLBACKS
// ------------------------------

static void ui_home_screen_set_wifi_ssid_job(void *arg)
{
    ui_home_screen_label_job_t *job = (ui_home_screen_label_job_t *)arg;
    ui_child_t *c = &home_screen.children[job->child_index];

    if(c->lv_obj)
    {
        lv_label_set_text(c->lv_obj, job->text);
    }
}



static void ui_home_screen_set_discovery_msg_job(void *arg)
{
    ui_home_screen_label_job_t *job = (ui_home_screen_label_job_t *)arg;
    ui_child_t *c = &home_screen.children[job->child_index];

    if(c->lv_obj)
    {
        lv_label_set_text(c->lv_obj, job->text);
    }
}



static void ui_home_screen_set_main_label_job(void *arg)
{
    ui_home_screen_label_job_t *job = (ui_home_screen_label_job_t *)arg;
    ui_child_t *c = &home_screen.children[job->child_index];

    if(c->lv_obj)
    {
        lv_label_set_text(c->lv_obj, job->text);
    }
}


// ------------------------------
// UI SETTERS
// ------------------------------

void ui_home_screen_set_wifi_ssid(const char *text)
{
    ui_home_screen_label_job_t job;
    job.child_index = 0;
    snprintf(job.text, UI_MAX_STRING_LENGTH, "%s", text);

    ui_worker_process_job(ui_home_screen_set_wifi_ssid_job, &job, sizeof(job));
}



void ui_home_screen_set_discovery_msg(const char *text)
{
    ui_home_screen_label_job_t job;
    job.child_index = 1;
    snprintf(job.text, UI_MAX_STRING_LENGTH, "%s", text);

    ui_worker_process_job(ui_home_screen_set_discovery_msg_job, &job, sizeof(job));
}



void ui_home_screen_set_main_label(const char *text)
{
    ui_home_screen_label_job_t job;
    job.child_index = 2;
    snprintf(job.text, UI_MAX_STRING_LENGTH, "%s", text);

    ui_worker_process_job(ui_home_screen_set_main_label_job, &job, sizeof(job));
}



static void ui_home_load_screen_cb(void* args){

    printf("\nboot screen...");
    lv_scr_load(home_screen.lv_screen);
    printf("\nloaded...\n");

     //for(int i = 0; i < boot_screen.child_count; i++){
       // ui_child_t *c = &boot_screen.children[i];
           
}


void ui_home_load_screen(){

   ui_worker_process_job(ui_home_load_screen_cb, NULL, 0);

}



// ------------------------------
// SCREEN INIT
// ------------------------------
// ------------------------------
// SCREEN INIT
// ------------------------------
void ui_home_init(void)
{
    home_screen.lv_screen = lv_obj_create(NULL);

    for (int i = 0; i < home_screen.child_count; i++)
    {
        ui_child_t *c = &home_screen.children[i];

        switch (c->type)
        {

            case UI_CHILD_LABEL:
                c->lv_obj = lv_label_create(home_screen.lv_screen);
                lv_obj_set_pos(c->lv_obj, c->x, c->y);
                lv_obj_set_width(c->lv_obj, c->w);
                lv_label_set_long_mode(c->lv_obj, LV_LABEL_LONG_CLIP);
                break;
                

                case UI_CHILD_ICON:
                    c->lv_obj = lv_img_create(home_screen.lv_screen);
                    lv_obj_set_pos(c->lv_obj, c->x, c->y);
                    lv_obj_set_size(c->lv_obj, c->w, c->h);
                    lv_obj_set_style_clip_corner(
                        c->lv_obj,
                        true,
                        LV_PART_MAIN | LV_STATE_DEFAULT
                    );
                    lv_image_set_inner_align(c->lv_obj, LV_IMAGE_ALIGN_CENTER);
                    break;
        

                case UI_CHILD_BAR:
                    c->lv_obj = lv_bar_create(home_screen.lv_screen);
                    lv_obj_set_pos(c->lv_obj, c->x, c->y);
                    lv_obj_set_size(c->lv_obj, c->w, c->h);
                    lv_bar_set_value(
                        c->lv_obj,
                        c->initial_value,
                        LV_ANIM_OFF
                    );
                    break;
        
            default:
                break;
        }
    }
}
