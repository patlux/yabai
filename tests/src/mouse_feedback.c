// A mouse drag over a tiled window shows the insert feedback on that window.
// Every mouse-up must remove it, also when the drop does not change the tree,
// or the overlay stays on screen until a new window consumes it.
//
// The nodes have no feedback window id, so insert_feedback_destroy does not
// touch the window server and these tests never draw an overlay.
static void test_feedback_reset(struct window_node *node, int insert_dir)
{
    memset(node, 0, sizeof(struct window_node));
    node->insert_dir = insert_dir;
    memset(&g_mouse_state, 0, sizeof(struct mouse_state));
    g_mouse_state.current_action = MOUSE_MODE_MOVE;
    g_mouse_state.feedback_node = node;
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
}

TEST_FUNC(mouse_up_without_window_clears_feedback,
{
    struct window_node node;
    test_feedback_reset(&node, DIR_WEST);

    EVENT_HANDLER_MOUSE_UP((void *) CGEventCreate(NULL), 0);

    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(node.insert_dir, 0);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_NONE);
});

TEST_FUNC(mouse_up_with_invalid_window_clears_feedback,
{
    struct window_node node;
    struct window window = {0};
    test_feedback_reset(&node, DIR_EAST);
    // A window the system marked invalid no longer points id_ptr at its id.
    window.id = 0x7fffffd1;
    g_mouse_state.window = &window;

    EVENT_HANDLER_MOUSE_UP((void *) CGEventCreate(NULL), 0);

    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(node.insert_dir, 0);
    TEST_CHECK(g_mouse_state.window == NULL, true);
});

TEST_FUNC(mouse_drag_with_invalid_window_clears_feedback,
{
    struct window_node node;
    struct window window = {0};
    test_feedback_reset(&node, DIR_NORTH);
    window.id = 0x7fffffd2;
    g_mouse_state.window = &window;

    EVENT_HANDLER_MOUSE_DRAGGED((void *) CGEventCreate(NULL), 0);

    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(node.insert_dir, 0);
    TEST_CHECK(g_mouse_state.current_action, MOUSE_MODE_NONE);
});

// The node under the cursor can close during a drag. The mouse state must not
// keep the freed node, or the next mouse-up writes to freed memory.
TEST_FUNC(window_removal_forgets_feedback_node,
{
    g_connection = SLSMainConnectionID();
    struct window_node *root = calloc(1, sizeof(struct window_node));
    struct view view = {0};
    view.sid = display_space_id(CGMainDisplayID());
    view.root = root;
    view.layout = VIEW_BSP;
    root->split = SPLIT_Y;
    root->ratio = 0.5f;
    root->left = test_removal_leaf(0x7fffffd3, root);
    root->right = test_removal_leaf(0x7fffffd4, root);
    view_update(&view);

    memset(&g_mouse_state, 0, sizeof(struct mouse_state));
    root->left->insert_dir = DIR_WEST;
    g_mouse_state.feedback_node = root->left;

    struct window removed = {0};
    removed.id = 0x7fffffd3;
    view_remove_window_node(&view, &removed);

    TEST_CHECK(g_mouse_state.feedback_node == NULL, true);
    TEST_CHECK(root->insert_dir, 0);
    free(root);
});
