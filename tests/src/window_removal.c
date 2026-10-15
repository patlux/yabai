// Synthetic trees, real read-only display bounds. Never flush these made-up
// windows: removing a node must refresh geometry without touching the desktop.
static struct window_node *test_removal_leaf(uint32_t wid, struct window_node *parent)
{
    struct window_node *node = calloc(1, sizeof(struct window_node));
    node->parent = parent;
    node->window_count = 1;
    node->window_list[0] = wid;
    node->window_order[0] = wid;
    return node;
}

static bool test_removal_refresh(bool remove_left, bool subtree, bool auto_balance)
{
    g_connection = SLSMainConnectionID();
    uint32_t did = CGMainDisplayID();
    uint64_t sid = display_space_id(did);
    if (!sid) return false;

    struct window_node *root = calloc(1, sizeof(struct window_node));
    struct view view = {0};
    view.sid = sid;
    view.root = root;
    view.layout = VIEW_BSP;
    view.auto_balance = auto_balance ? SPLIT_X : SPLIT_NONE;
    view.left_padding = 10;
    view.right_padding = 10;
    view.top_padding = 10;
    view.bottom_padding = 10;
    view.window_gap = 6;
    view_set_flag(&view, VIEW_ENABLE_PADDING);
    view_set_flag(&view, VIEW_ENABLE_GAP);

    root->split = SPLIT_X;
    root->ratio = 0.37f;
    root->left = test_removal_leaf(0x7fffffe1, root);
    root->right = test_removal_leaf(0x7fffffe2, root);
    struct window_node *survivor = remove_left ? root->right : root->left;
    if (subtree) {
        survivor->window_count = 0;
        survivor->split = SPLIT_X;
        survivor->ratio = 0.37f;
        survivor->left = test_removal_leaf(0x7fffffe3, survivor);
        survivor->right = test_removal_leaf(0x7fffffe4, survivor);
    }

    view_update(&view);
    struct area expected = root->area;
    // Model the cached, narrower usable area left by an earlier Dock state.
    root->area.w -= 56;
    window_node_update(&view, root);
    view_clear_flag(&view, VIEW_IS_DIRTY);

    struct window removed = {0};
    removed.id = remove_left ? 0x7fffffe1 : 0x7fffffe2;
    struct window_node *changed = view_remove_window_node(&view, &removed);
    bool ok = changed == root &&
              root->area.x == expected.x && root->area.y == expected.y &&
              root->area.w == expected.w && root->area.h == expected.h &&
              view_is_dirty(&view) && !view_is_invalid(&view) &&
              view_find_window_node(&view, removed.id) == NULL;
    if (subtree) {
        ok = ok && root->left && root->right;
        if (root->left && root->right) {
            ok = ok && root->right->area.x + root->right->area.w == expected.x + expected.w;
            if (!auto_balance) ok = ok && root->ratio == 0.37f;
            free(root->left);
            free(root->right);
        }
    } else {
        ok = ok && root->window_count == 1;
    }
    free(root);
    return ok;
}

TEST_FUNC(window_removal_refreshes_left_survivor,
{
    TEST_CHECK(test_removal_refresh(false, false, false), true);
});

TEST_FUNC(window_removal_refreshes_right_survivor,
{
    TEST_CHECK(test_removal_refresh(true, false, false), true);
});

TEST_FUNC(window_removal_refreshes_subtree_without_balancing,
{
    TEST_CHECK(test_removal_refresh(true, true, false), true);
});

TEST_FUNC(window_removal_keeps_explicit_auto_balance,
{
    TEST_CHECK(test_removal_refresh(false, true, true), true);
});

// A removal below the root must also flush the unaffected sibling: refreshing
// display bounds changes its geometry even though its own subtree is unchanged.
TEST_FUNC(window_removal_refreshes_entire_tree,
{
    g_connection = SLSMainConnectionID();
    struct window_node *root = calloc(1, sizeof(struct window_node));
    struct view view = {0};
    view.sid = display_space_id(CGMainDisplayID());
    view.root = root;
    view.layout = VIEW_BSP;
    root->split = SPLIT_X;
    root->ratio = 0.37f;
    root->left = calloc(1, sizeof(struct window_node));
    root->left->parent = root;
    root->left->split = SPLIT_Y;
    root->left->ratio = 0.61f;
    root->left->left = test_removal_leaf(0x7fffffe1, root->left);
    root->left->right = test_removal_leaf(0x7fffffe2, root->left);
    root->right = test_removal_leaf(0x7fffffe3, root);
    view_update(&view);
    struct area expected = root->right->area;
    root->area.w -= 56;
    window_node_update(&view, root);

    struct window removed = {0};
    removed.id = 0x7fffffe1;
    TEST_CHECK(view_remove_window_node(&view, &removed) == root, true);
    TEST_CHECK(root->right->area.x == expected.x, true);
    TEST_CHECK(root->right->area.w == expected.w, true);
    TEST_CHECK(root->ratio == 0.37f, true);
    TEST_CHECK(root->left->window_list[0] == 0x7fffffe2, true);
    free(root->left);
    free(root->right);
    free(root);
});
