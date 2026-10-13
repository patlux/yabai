// update_window_notifications() subscribes to every tracked window from Sequoia on.
// The list used to live in a fixed 1024-entry stack array, so tracking more windows
// overflowed it. The ids are made up; nothing is subscribed to on screen.
#define TEST_NOTIFY_WINDOW_COUNT 1500
#define TEST_NOTIFY_WID_BASE     0x7f000000

TEST_FUNC(window_notification_list_includes_every_tracked_window,
{
    g_connection = SLSMainConnectionID();
    workspace_set_macos_version(26);

    struct table saved = g_window_manager.window;
    table_init(&g_window_manager.window, 150, hash_wm, compare_wm);

    struct application application = { .name = "test" };
    struct window *windows = calloc(TEST_NOTIFY_WINDOW_COUNT, sizeof(struct window));
    for (int i = 0; i < TEST_NOTIFY_WINDOW_COUNT; ++i) {
        windows[i].application = &application;
        windows[i].id = TEST_NOTIFY_WID_BASE + i;
        windows[i].id_ptr = &windows[i].id;
        window_manager_add_window(&g_window_manager, &windows[i]);
    }

    int count = 0;
    uint32_t *list = window_notification_list(&count);
    TEST_CHECK(list != NULL, true);
    TEST_CHECK(count, TEST_NOTIFY_WINDOW_COUNT);

    uint64_t sum = 0;
    for (int i = 0; i < count; ++i) sum += list[i] - TEST_NOTIFY_WID_BASE;
    TEST_CHECK(sum == (uint64_t) TEST_NOTIFY_WINDOW_COUNT * (TEST_NOTIFY_WINDOW_COUNT - 1) / 2, true);
    free(list);

    update_window_notifications();

    table_free(&g_window_manager.window);
    g_window_manager.window = saved;
    free(windows);
})
