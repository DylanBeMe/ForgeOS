#include "state.h"

int fs_ui_state_wrap_page(int page, int delta, int page_count) {
    int next;
    if (page_count <= 0) return 0;
    next = page + delta;
    while (next < 0) next += page_count;
    while (next >= page_count) next -= page_count;
    return next;
}

int fs_ui_state_wrap_selection(int selection, int delta, int item_count) {
    int next;
    if (item_count <= 0) return 0;
    next = selection + delta;
    while (next < 0) next += item_count;
    while (next >= item_count) next -= item_count;
    return next;
}

int fs_ui_state_clamp_selection(int selection, int item_count) {
    if (item_count <= 0) return 0;
    if (selection < 0) return 0;
    if (selection >= item_count) return item_count - 1;
    return selection;
}
