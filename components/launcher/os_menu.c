// Menu: the home of the watch features, apps and settings.
#include "os_screens.h"

typedef struct {
    const char *icon;
    const char *label;
    const os_screen_t *screen; // NULL: not built yet (os_soon_screen)
} menu_item_t;

static const menu_item_t ITEMS[] = {
    {OS_ICON_APPS, "Apps", &os_apps_screen},
    {OS_ICON_STOPWATCH, "Cronómetro", &os_stopwatch_screen},
    {OS_ICON_HOURGLASS, "Temporizador", NULL},
    {OS_ICON_BELL, "Alarmas", NULL},
    {OS_ICON_FLASHLIGHT, "Linterna", &os_flashlight_screen},
    {OS_ICON_SETTINGS, "Ajustes", &os_settings_screen},
};
#define ITEM_COUNT (int)(sizeof(ITEMS) / sizeof(ITEMS[0]))

static int s_last_focus;

static void item_clicked(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_last_focus = i;
    if (ITEMS[i].screen != NULL) {
        os_push(ITEMS[i].screen);
    } else {
        os_soon_set_title(ITEMS[i].label);
        os_push(&os_soon_screen);
    }
}

static void menu_create(lv_obj_t *root)
{
    os_title(root, "Menú");

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 342, LV_SIZE_CONTENT);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 92);
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_pad_row(grid, 22, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 10, 0);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_CENTER, i % 3, 1, LV_GRID_ALIGN_START, i / 3, 1);
        lv_obj_t *button = os_round_button(cell, 84, ITEMS[i].icon, &font_icons_32, item_clicked, (void *)(intptr_t)i);
        if (ITEMS[i].screen == NULL) {
            lv_obj_set_style_text_opa(button, LV_OPA_50, 0);
        }
        os_focus_add(button);
        os_label(cell, &font_barlow_16, ITEMS[i].screen != NULL ? OS_TEXT : OS_MUTED, ITEMS[i].label);
    }
    os_focus_set(s_last_focus);
    os_hint(root, "BOOT abrir · PWR volver");
}

const os_screen_t os_menu_screen = {
    .name = "menu",
    .create = menu_create,
};
