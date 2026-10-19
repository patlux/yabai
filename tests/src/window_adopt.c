// A window that gains focus without yabai ever having seen it created is adopted from its application.
// The window id is made up, so no process owns it and no real window is touched.
#define TEST_ADOPT_WID 0x7ffffff1

TEST_FUNC(window_adopt_skips_a_window_no_application_owns,
{
    if (!g_window_manager.application.buckets) {
        table_init(&g_window_manager.application, 150, hash_wm, compare_wm);
    }

    if (!g_window_manager.window_adopt_attempted.buckets) {
        table_init(&g_window_manager.window_adopt_attempted, 150, hash_wm, compare_wm);
    }

    g_connection = SLSMainConnectionID();
    TEST_CHECK(window_manager_adopt_missed_window(&g_window_manager, NULL, TEST_ADOPT_WID), false);
    TEST_CHECK(table_find(&g_window_manager.window_adopt_attempted, &(uint32_t){TEST_ADOPT_WID}) == NULL, true);
});
