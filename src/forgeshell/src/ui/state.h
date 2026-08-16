#ifndef FORGESHELL_UI_STATE_H
#define FORGESHELL_UI_STATE_H

typedef enum {
    PAGE_HOME = 0,
    PAGE_LIBRARY,
    PAGE_SEARCH,
    PAGE_ACTIVITY,
    PAGE_TOOLS,
    PAGE_SETTINGS,
    PAGE_POWER,
    PAGE_COUNT
} FsPage;

int fs_ui_state_wrap_page(int page, int delta, int page_count);
int fs_ui_state_wrap_selection(int selection, int delta, int item_count);
int fs_ui_state_clamp_selection(int selection, int item_count);

#endif
