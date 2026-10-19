// A window whose AXUIElementRef has gone stale is looked up again in its application's window list.
// The pid is made up, so the application answers nothing and no real window is touched.
#define TEST_REF_PID 0x7ffffff0

TEST_FUNC(window_ref_kept_when_the_application_has_no_such_window,
{
    struct application application = {0};
    application.name = "test";
    application.ref = AXUIElementCreateApplication(TEST_REF_PID);

    struct window window = {0};
    window.application = &application;
    window.id = 0x7ffffff1;
    window.ref = AXUIElementCreateApplication(TEST_REF_PID);
    AXUIElementRef stale_ref = window.ref;

    TEST_CHECK(window_manager_refresh_window_ref(&window), false);
    TEST_CHECK(window.ref == stale_ref, true);

    CFRelease(window.ref);
    CFRelease(application.ref);
});
