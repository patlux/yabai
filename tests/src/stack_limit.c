// A node keeps its windows in arrays of NODE_MAX_WINDOW_COUNT entries. A space
// with a stack layout is a single node, and `window --insert stack` stacks onto
// a leaf; neither checked the limit, so the next window was written past the
// end of the arrays. The ids are made up and the spaces are not visible, so
// nothing here moves a real window.
#define TEST_STACK_SID 0x7fffffc0ULL
#define TEST_STACK_WID 0x7fffe000

static struct view *test_stack_view(enum view_type layout)
{
    struct view *view = calloc(1, sizeof(struct view));
    view->root = calloc(1, sizeof(struct window_node));
    view->sid = TEST_STACK_SID;
    view->layout = layout;
    return view;
}

TEST_FUNC(stack_layout_leaves_out_a_window_past_the_limit,
{
    struct view *view = test_stack_view(VIEW_STACK);
    struct window windows[NODE_MAX_WINDOW_COUNT + 1] = {0};
    for (int i = 0; i <= NODE_MAX_WINDOW_COUNT; ++i) windows[i].id = TEST_STACK_WID + i;
    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) view_add_window_node(view, &windows[i]);

    struct window *extra = &windows[NODE_MAX_WINDOW_COUNT];
    TEST_CHECK(view_add_window_node(view, extra) == NULL, true);
    TEST_CHECK(view->root->window_count, NODE_MAX_WINDOW_COUNT);
    TEST_CHECK(window_node_contains_window(view->root, extra->id), false);
    TEST_CHECK(view->root->window_order[0] == windows[NODE_MAX_WINDOW_COUNT - 1].id, true);
    free(view->root);
    free(view);
});

TEST_FUNC(insert_stack_tiles_beside_a_full_stack,
{
    struct view *view = test_stack_view(VIEW_BSP);
    struct window windows[NODE_MAX_WINDOW_COUNT] = {0};
    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) windows[i].id = TEST_STACK_WID + 0x100 + i;
    view_add_window_node(view, &windows[0]);
    for (int i = 1; i < NODE_MAX_WINDOW_COUNT; ++i) view_stack_window_node(view->root, &windows[i]);
    view->root->insert_dir = STACK;
    view->insertion_point = windows[0].id;

    struct window extra = { .id = TEST_STACK_WID + 0x200 };
    view_add_window_node(view, &extra);
    struct window_node *stack = view_find_window_node(view, windows[0].id);
    struct window_node *tile = view_find_window_node(view, extra.id);
    TEST_CHECK(stack != NULL, true);
    TEST_CHECK(tile != NULL, true);
    TEST_CHECK(stack != tile, true);
    if (stack) TEST_CHECK(stack->window_count, NODE_MAX_WINDOW_COUNT);
    if (tile)  TEST_CHECK(tile->window_count, 1);
    if (view->root->left)  free(view->root->left);
    if (view->root->right) free(view->root->right);
    free(view->root);
    free(view);
});
