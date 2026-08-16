#include "ui/state.h"

#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void) {
    int page = PAGE_HOME;
    page = fs_ui_state_wrap_page(page, -1, PAGE_COUNT);
    CHECK(page == PAGE_POWER);
    page = fs_ui_state_wrap_page(page, 1, PAGE_COUNT);
    CHECK(page == PAGE_HOME);
    page = fs_ui_state_wrap_page(PAGE_LIBRARY, 2, PAGE_COUNT);
    CHECK(page == PAGE_ACTIVITY);

    CHECK(fs_ui_state_wrap_selection(0, -1, 4) == 3);
    CHECK(fs_ui_state_wrap_selection(3, 1, 4) == 0);
    CHECK(fs_ui_state_wrap_selection(1, 5, 4) == 2);
    CHECK(fs_ui_state_wrap_selection(4, -6, 4) == 2);
    CHECK(fs_ui_state_wrap_selection(99, 1, 0) == 0);

    CHECK(fs_ui_state_clamp_selection(-2, 4) == 0);
    CHECK(fs_ui_state_clamp_selection(2, 4) == 2);
    CHECK(fs_ui_state_clamp_selection(9, 4) == 3);
    CHECK(fs_ui_state_clamp_selection(9, 0) == 0);
    return 0;
}
