
#ifndef UI_HOME_H
#define UI_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------
// API
// ------------------------------
void ui_home_init(void);
void ui_home_load_screen();
void ui_home_screen_set_wifi_ssid(const char *text);
void ui_home_screen_set_discovery_msg(const char *text);
void ui_home_screen_set_main_label(const char *text);

#ifdef __cplusplus
}
#endif

#endif
