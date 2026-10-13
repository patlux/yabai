extern uint8_t *g_event_bytes;
extern struct event_loop g_event_loop;
extern void *g_workspace_context;
extern struct process_manager g_process_manager;
extern struct mouse_state g_mouse_state;

static TABLE_HASH_FUNC(hash_wm)
{
    return *(uint32_t *) key;
}

static TABLE_COMPARE_FUNC(compare_wm)
{
    return *(uint32_t *) key_a == *(uint32_t *) key_b;
}

bool window_manager_is_window_eligible(struct window *window)
{
    bool result = window->is_root && (window_is_real(window) || window_check_rule_flag(window, WINDOW_RULE_MANAGED));
    return result;
}

void window_manager_query_window_rules(FILE *rsp)
{
    TIME_FUNCTION;

    fprintf(rsp, "[");
    for (int i = 0; i < buf_len(g_window_manager.rules); ++i) {
        struct rule *rule = &g_window_manager.rules[i];
        rule_serialize(rsp, rule, i);
        if (i < buf_len(g_window_manager.rules) - 1) fprintf(rsp, ",");
    }
    fprintf(rsp, "]\n");
}

void window_manager_query_windows_for_spaces(FILE *rsp, uint64_t *space_list, int space_count, uint64_t flags)
{
    TIME_FUNCTION;

    int window_count = 0;
    uint32_t *window_list = space_window_list_for_connection(space_list, space_count, 0, &window_count, true);

    fprintf(rsp, "[");
    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_manager_find_window(&g_window_manager, window_list[i]);
        if (window) window_serialize(rsp, window, flags); else window_nonax_serialize(rsp, window_list[i], flags);
        if (i < window_count - 1) fprintf(rsp, ",");
    }
    fprintf(rsp, "]\n");
}

void window_manager_query_windows_for_display(FILE *rsp, uint32_t did, uint64_t flags)
{
    TIME_FUNCTION;

    int space_count = 0;
    uint64_t *space_list = display_space_list(did, &space_count);
    window_manager_query_windows_for_spaces(rsp, space_list, space_count, flags);
}

void window_manager_query_windows_for_displays(FILE *rsp, uint64_t flags)
{
    TIME_FUNCTION;

    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);

    int space_count = 0;
    uint64_t *space_list = NULL;

    for (int i = 0; i < display_count; ++i) {
        int count;
        uint64_t *list = display_space_list(display_list[i], &count);
        if (!list) continue;

        //
        // NOTE(asmvik): display_space_list(..) uses a linear allocator,
        // and so we only need to track the beginning of the first list along
        // with the total number of spaces that have been allocated.
        //

        if (!space_list) space_list = list;
        space_count += count;
    }

    window_manager_query_windows_for_spaces(rsp, space_list, space_count, flags);
}

bool window_manager_rule_matches_window(struct rule *rule, struct window *window, char *window_title, char *window_role, char *window_subrole)
{
    int regex_match_app = rule_check_flag(rule, RULE_APP_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_APP_VALID), &rule->app_regex, window->application->name) == regex_match_app) return false;

    int regex_match_title = rule_check_flag(rule, RULE_TITLE_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_TITLE_VALID), &rule->title_regex, window_title) == regex_match_title) return false;

    int regex_match_role = rule_check_flag(rule, RULE_ROLE_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_ROLE_VALID), &rule->role_regex, window_role) == regex_match_role) return false;

    int regex_match_subrole = rule_check_flag(rule, RULE_SUBROLE_EXCLUDE) ? REGEX_MATCH_YES : REGEX_MATCH_NO;
    if (regex_match(rule_check_flag(rule, RULE_SUBROLE_VALID), &rule->subrole_regex, window_subrole) == regex_match_subrole) return false;

    return true;
}

void window_manager_apply_manage_rule_effects_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, struct rule_effects *effects)
{
    if (effects->manage == RULE_PROP_ON) {
        window_set_rule_flag(window, WINDOW_RULE_MANAGED);
        window_manager_make_window_floating(sm, wm, window, false, true);
    } else if (effects->manage == RULE_PROP_OFF) {
        window_clear_rule_flag(window, WINDOW_RULE_MANAGED);
        window_manager_make_window_floating(sm, wm, window, true, true);
    }
}

void window_manager_apply_rule_effects_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, struct rule_effects *effects)
{
    if (effects->sid || effects->did) {
        if (!window_is_fullscreen(window) && !space_is_fullscreen(window_space(window->id))) {
            uint64_t sid = effects->sid ? effects->sid : display_space_id(effects->did);
            window_manager_send_window_to_space(sm, wm, window, sid, true);
            if (rule_effects_check_flag(effects, RULE_FOLLOW_SPACE) || effects->fullscreen == RULE_PROP_ON) {
                space_manager_focus_space(sid);
            }
        }
    }

    if (effects->mff == RULE_PROP_ON) {
        window_set_rule_flag(window, WINDOW_RULE_MFF);
        window_set_rule_flag(window, WINDOW_RULE_MFF_VALUE);
    } else if (effects->mff == RULE_PROP_OFF) {
        window_set_rule_flag(window, WINDOW_RULE_MFF);
        window_clear_rule_flag(window, WINDOW_RULE_MFF_VALUE);
    }

    if (effects->fullscreen == RULE_PROP_ON) {
        AXUIElementSetAttributeValue(window->ref, kAXFullscreenAttribute, kCFBooleanTrue);
        window_set_rule_flag(window, WINDOW_RULE_FULLSCREEN);
    }

    if (effects->grid[0] != 0 && effects->grid[1] != 0) {
        window_manager_apply_grid(sm, wm, window, effects->grid[0], effects->grid[1], effects->grid[2], effects->grid[3], effects->grid[4], effects->grid[5]);
    }
}

void window_manager_apply_manage_rules_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, char *window_title, char *window_role, char *window_subrole, bool one_shot_rules)
{
    bool match = false;
    struct rule_effects effects = {0};

    for (int i = 0; i < buf_len(wm->rules); ++i) {
        if (one_shot_rules || !rule_check_flag(&wm->rules[i], RULE_ONE_SHOT)) {
            if (window_manager_rule_matches_window(&wm->rules[i], window, window_title, window_role, window_subrole)) {
                if (wm->rules[i].effects.manage == RULE_PROP_ON) {
                    if (!rule_check_flag(&wm->rules[i], RULE_ROLE_VALID)    && !string_equals(window_role   , "AXWindow"))         continue;
                    if (!rule_check_flag(&wm->rules[i], RULE_SUBROLE_VALID) && !string_equals(window_subrole, "AXStandardWindow")) continue;
                }

                match = true;
                rule_combine_effects(&wm->rules[i].effects, &effects);

                if (rule_check_flag(&wm->rules[i], RULE_ONE_SHOT)) rule_set_flag(&wm->rules[i], RULE_ONE_SHOT_REMOVE);
            }
        }
    }

    if (match) window_manager_apply_manage_rule_effects_to_window(sm, wm, window, &effects);
}

void window_manager_apply_rules_to_window(struct space_manager *sm, struct window_manager *wm, struct window *window, char *window_title, char *window_role, char *window_subrole, bool one_shot_rules)
{
    bool match = false;
    struct rule_effects effects = {0};

    for (int i = 0; i < buf_len(wm->rules); ++i) {
        if (one_shot_rules || !rule_check_flag(&wm->rules[i], RULE_ONE_SHOT)) {
            if (window_manager_rule_matches_window(&wm->rules[i], window, window_title, window_role, window_subrole)) {
                if (!window_check_rule_flag(window, WINDOW_RULE_MANAGED)) {
                    if (!rule_check_flag(&wm->rules[i], RULE_ROLE_VALID)    && !string_equals(window_role   , "AXWindow"))         continue;
                    if (!rule_check_flag(&wm->rules[i], RULE_SUBROLE_VALID) && !string_equals(window_subrole, "AXStandardWindow")) continue;
                }

                match = true;
                rule_combine_effects(&wm->rules[i].effects, &effects);

                if (rule_check_flag(&wm->rules[i], RULE_ONE_SHOT)) rule_set_flag(&wm->rules[i], RULE_ONE_SHOT_REMOVE);
            }
        }
    }

    if (match) window_manager_apply_rule_effects_to_window(sm, wm, window, &effects);
}

void window_manager_set_focus_follows_mouse(struct window_manager *wm, enum ffm_mode mode)
{
    mouse_handler_end(&g_mouse_state);

    if (mode == FFM_DISABLED) {
        mouse_handler_begin(&g_mouse_state, MOUSE_EVENT_MASK);
    } else {
        mouse_handler_begin(&g_mouse_state, MOUSE_EVENT_MASK_FFM);
    }

    wm->ffm_mode = mode;
}

void window_manager_center_mouse(struct window_manager *wm, struct window *window)
{
    if (window_check_rule_flag(window, WINDOW_RULE_MFF)) {
        if (!window_check_rule_flag(window, WINDOW_RULE_MFF_VALUE)) {
            return;
        }
    } else {
        if (!wm->enable_mff) {
            return;
        }
    }

    CGPoint cursor;
    SLSGetCurrentCursorLocation(g_connection, &cursor);
    if (CGRectContainsPoint(window->frame, cursor)) return;

    uint32_t did = window_display_id(window->id);
    if (!did) return;

    CGPoint center = {
        window->frame.origin.x + window->frame.size.width / 2,
        window->frame.origin.y + window->frame.size.height / 2
    };

    CGRect bounds = CGDisplayBounds(did);
    if (!CGRectContainsPoint(bounds, center)) return;

    CGWarpMouseCursorPosition(center);
}

bool window_manager_should_manage_window(struct window *window)
{
    if (!window->is_root)                              return false;
    if (window_check_flag(window, WINDOW_FLOAT))       return false;
    if (window_is_sticky(window->id))                  return false;
    if (window_check_flag(window, WINDOW_MINIMIZE))    return false;
    if (window_check_flag(window, WINDOW_ORDERED_OUT)) return false;
    if (window->application->is_hidden)                return false;

    return (window_is_standard(window) && window_level_is_standard(window) && window_can_move(window)) || window_check_rule_flag(window, WINDOW_RULE_MANAGED);
}

struct view *window_manager_find_managed_window(struct window_manager *wm, struct window *window)
{
    return table_find(&wm->managed_window, &window->id);
}

void window_manager_remove_managed_window(struct window_manager *wm, uint32_t wid)
{
    table_remove(&wm->managed_window, &wid);
}

void window_manager_add_managed_window(struct window_manager *wm, struct window *window, struct view *view)
{
    if (view->layout == VIEW_FLOAT) return;
    table_add(&wm->managed_window, &window->id, view);
}

void window_manager_window_did_order_out(struct window_manager *wm, struct window *window)
{
    window_set_flag(window, WINDOW_ORDERED_OUT);

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(wm, window->id);
    }
}

// NOTE: sid is the active space when the window is on it, and 0 otherwise; a window
// ordered in on an inactive space is tiled when that space becomes active.
void window_manager_window_did_order_in(struct window_manager *wm, struct window *window, uint64_t sid)
{
    window_clear_flag(window, WINDOW_ORDERED_OUT);

    if (!sid) return;
    if (!window_manager_should_manage_window(window)) return;
    if (window_manager_find_managed_window(wm, window)) return;

    struct window *last_window = window_manager_find_window(wm, wm->last_window_id);
    uint32_t insertion_point = last_window && last_window->application->pid != window->application->pid ? last_window->id : 0;
    struct view *view = space_manager_tile_window_on_space_with_insertion_point(&g_space_manager, window, sid, insertion_point);
    window_manager_add_managed_window(wm, window, view);
}

enum window_op_error window_manager_adjust_window_ratio(struct window_manager *wm, struct window *window, int type, float ratio)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    switch (type) {
    case TYPE_REL: {
        node->parent->ratio = clampf_range(node->parent->ratio + ratio, 0.1f, 0.9f);
    } break;
    case TYPE_ABS: {
        node->parent->ratio = clampf_range(ratio, 0.1f, 0.9f);
    } break;
    }

    window_node_update(view, node->parent);

    if (space_is_visible(view->sid)) {
        window_node_flush(node->parent);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }

    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_move_window_relative(struct window_manager *wm, struct window *window, int type, float dx, float dy)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    if (type == TYPE_REL) {
        dx += window->frame.origin.x;
        dy += window->frame.origin.y;
    }

    window_manager_animate_window((struct window_capture) { .window = window, .x = dx, .y = dy, .w = window->frame.size.width, .h = window->frame.size.height });
    return WINDOW_OP_ERROR_SUCCESS;
}

void window_manager_resize_window_relative_internal(struct window *window, CGRect frame, int direction, float dx, float dy, bool animate)
{
    TIME_FUNCTION;

    int x_mod = (direction & HANDLE_LEFT) ? -1 : (direction & HANDLE_RIGHT)  ? 1 : 0;
    int y_mod = (direction & HANDLE_TOP)  ? -1 : (direction & HANDLE_BOTTOM) ? 1 : 0;

    float fw = max(1, frame.size.width  + dx * x_mod);
    float fh = max(1, frame.size.height + dy * y_mod);
    float fx = (direction & HANDLE_LEFT) ? frame.origin.x + frame.size.width  - fw : frame.origin.x;
    float fy = (direction & HANDLE_TOP)  ? frame.origin.y + frame.size.height - fh : frame.origin.y;

    if (animate) {
        window_manager_animate_window((struct window_capture) { .window = window, .x = fx, .y = fy, .w = fw, .h = fh });
    } else {
        AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
            window_manager_move_window(window, fx, fy);
            window_manager_resize_window(window, fw, fh);
        });
    }
}

enum window_op_error window_manager_resize_window_relative(struct window_manager *wm, struct window *window, int direction, float dx, float dy, bool animate)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) {
        if (direction == HANDLE_ABS) return WINDOW_OP_ERROR_INVALID_OPERATION;

        struct window_node *node = view_find_window_node(view, window->id);
        if (!node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

        struct window_node *x_fence = NULL;
        struct window_node *y_fence = NULL;

        if (direction & HANDLE_TOP)    x_fence = window_node_fence(node, DIR_NORTH);
        if (direction & HANDLE_BOTTOM) x_fence = window_node_fence(node, DIR_SOUTH);
        if (direction & HANDLE_LEFT)   y_fence = window_node_fence(node, DIR_WEST);
        if (direction & HANDLE_RIGHT)  y_fence = window_node_fence(node, DIR_EAST);
        if (!x_fence && !y_fence)      return WINDOW_OP_ERROR_INVALID_DST_NODE;

        if (y_fence) {
            float sr = y_fence->ratio + (float) dx / (float) y_fence->area.w;
            y_fence->ratio = clampf_range(sr, 0.1f, 0.9f);
        }

        if (x_fence) {
            float sr = x_fence->ratio + (float) dy / (float) x_fence->area.h;
            x_fence->ratio = clampf_range(sr, 0.1f, 0.9f);
        }

        view_update(view);
        view_flush(view);
    } else {
        if (direction == HANDLE_ABS) {
            if (animate) {
                window_manager_animate_window((struct window_capture) { .window = window, .x = window->frame.origin.x, .y = window->frame.origin.y, .w = dx, .h = dy });
            } else {
                AX_ENHANCED_UI_WORKAROUND(window->application->ref, { window_manager_resize_window(window, dx, dy); });
            }
        } else {
            window_manager_resize_window_relative_internal(window, window_ax_frame(window), direction, dx, dy, animate);
        }
    }

    return WINDOW_OP_ERROR_SUCCESS;
}

void window_manager_move_window(struct window *window, float x, float y)
{
    CGPoint position = CGPointMake(x, y);
    CFTypeRef position_ref = AXValueCreate(kAXValueTypeCGPoint, (void *) &position);
    if (!position_ref) return;

    AXUIElementSetAttributeValue(window->ref, kAXPositionAttribute, position_ref);
    CFRelease(position_ref);
}

void window_manager_resize_window(struct window *window, float width, float height)
{
    CGSize size = CGSizeMake(width, height);
    CFTypeRef size_ref = AXValueCreate(kAXValueTypeCGSize, (void *) &size);
    if (!size_ref) return;

    AXUIElementSetAttributeValue(window->ref, kAXSizeAttribute, size_ref);
    CFRelease(size_ref);
}

void window_manager_animate_window_list(struct window_capture *window_list, int window_count)
{
    TIME_FUNCTION;

    for (int i = 0; i < window_count; ++i) {
        window_manager_set_window_frame(window_list[i].window, window_list[i].x, window_list[i].y, window_list[i].w, window_list[i].h);
    }
}

void window_manager_animate_window(struct window_capture capture)
{
    TIME_FUNCTION;

    window_manager_set_window_frame(capture.window, capture.x, capture.y, capture.w, capture.h);
}

void window_manager_set_window_frame(struct window *window, float x, float y, float width, float height)
{
    //
    // NOTE(asmvik): Attempting to check the window frame cache to prevent unnecessary movement and resize calls to the AX API
    // is not reliable because it is possible to perform operations that should be applied, at a higher rate than the AX API events
    // are received, causing our cache to become out of date and incorrectly guard against some changes that **should** be applied.
    // This causes the window layout to **not** be modified the way we expect.
    //
    // A possible solution is to use the faster CG window notifications, as they are **a lot** more responsive, and can be used to
    // track changes to the window frame in real-time without delay.
    //

    AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
        CGPoint position = CGPointMake(x, y);
        CFTypeRef position_ref = AXValueCreate(kAXValueTypeCGPoint, (void *) &position);

        CGSize size = CGSizeMake(width, height);
        CFTypeRef size_ref = AXValueCreate(kAXValueTypeCGSize, (void *) &size);

        // NOTE(asmvik): Due to macOS constraints (visible screen-area), we might need to resize the window *before* moving it.
        if (size_ref) AXUIElementSetAttributeValue(window->ref, kAXSizeAttribute, size_ref);

        if (position_ref) {
            AXUIElementSetAttributeValue(window->ref, kAXPositionAttribute, position_ref);
            CFRelease(position_ref);
        }

        // NOTE(asmvik): Due to macOS constraints (visible screen-area), we might need to resize the window *after* moving it.
        if (size_ref) {
            AXUIElementSetAttributeValue(window->ref, kAXSizeAttribute, size_ref);
            CFRelease(size_ref);
        }
    });
}

void window_manager_set_menubar_opacity(struct window_manager *wm, float opacity)
{
    wm->menubar_opacity = opacity;
    SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, opacity);
}

int window_manager_find_rank_of_window_in_list(uint32_t wid, uint32_t *window_list, int window_count)
{
    for (int i = 0, rank = 0; i < window_count; ++i) {
        if (window_list[i] == wid) {
            return rank;
        } else {
            ++rank;
        }
    }

    return INT_MAX;
}

struct window *window_manager_find_window_on_space_by_rank_filtering_window(struct window_manager *wm, uint64_t sid, int rank, uint32_t filter_wid)
{
    int count;
    uint32_t *window_list = space_window_list(sid, &count, false);
    if (!window_list) return NULL;

    struct window *result = NULL;
    for (int i = 0, j = 0; i < count; ++i) {
        if (window_list[i] == filter_wid) continue;

        struct window *window = window_manager_find_window(wm, window_list[i]);
        if (!window) continue;

        if (++j == rank) {
            result = window;
            break;
        }
    }

    return result;
}

static inline bool window_manager_window_connection_is_jankyborders(int window_cid)
{
    static char process_name[PROC_PIDPATHINFO_MAXSIZE];

    pid_t window_pid = 0;
    SLSConnectionGetPID(window_cid, &window_pid);
    proc_name(window_pid, process_name, sizeof(process_name));

    return strcmp(process_name, "borders") == 0;
}

struct window *window_manager_find_window_at_point_filtering_window(struct window_manager *wm, CGPoint point, uint32_t filter_wid)
{
    CGPoint window_point;
    uint32_t window_id;
    int window_cid;

    SLSFindWindowAndOwner(g_connection, filter_wid, -1, 0, &point, &window_point, &window_id, &window_cid);
    if (g_connection == window_cid) SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid);

    if (window_manager_window_connection_is_jankyborders(window_cid)) {
        SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid);
        if (g_connection == window_cid) SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid);
    }

    return window_manager_find_window(wm, window_id);
}

struct window *window_manager_find_window_at_point(struct window_manager *wm, CGPoint point)
{
    CGPoint window_point;
    uint32_t window_id;
    int window_cid;

    SLSFindWindowAndOwner(g_connection, 0, 1, 0, &point, &window_point, &window_id, &window_cid);
    if (g_connection == window_cid) SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid);

    if (window_manager_window_connection_is_jankyborders(window_cid)) {
        SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid);
        if (g_connection == window_cid) SLSFindWindowAndOwner(g_connection, window_id, -1, 0, &point, &window_point, &window_id, &window_cid);
    }

    return window_manager_find_window(wm, window_id);
}

struct window *window_manager_find_window_below_cursor(struct window_manager *wm)
{
    CGPoint cursor;
    SLSGetCurrentCursorLocation(g_connection, &cursor);
    return window_manager_find_window_at_point(wm, cursor);
}

struct window *window_manager_find_closest_managed_window_in_direction(struct window_manager *wm, struct window *window, int direction)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    struct window_node *closest = view_find_window_node_in_direction(view, node, direction);
    if (!closest) return NULL;

    return window_manager_find_window(wm, closest->window_order[0]);
}

struct window *window_manager_find_prev_managed_window(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    struct window_node *prev = window_node_find_prev_leaf(node);
    if (!prev) return NULL;

    return window_manager_find_window(wm, prev->window_order[0]);
}

struct window *window_manager_find_next_managed_window(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    struct window_node *next = window_node_find_next_leaf(node);
    if (!next) return NULL;

    return window_manager_find_window(wm, next->window_order[0]);
}

struct window *window_manager_find_first_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *first = window_node_find_first_leaf(view->root);
    if (!first) return NULL;

    return window_manager_find_window(wm, first->window_order[0]);
}

struct window *window_manager_find_last_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *last = window_node_find_last_leaf(view->root);
    if (!last) return NULL;

    return window_manager_find_window(wm, last->window_order[0]);
}

struct window *window_manager_find_recent_managed_window(struct window_manager *wm)
{
    struct window *window = window_manager_find_window(wm, wm->last_window_id);
    if (!window) return NULL;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    return window;
}

struct window *window_manager_find_prev_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    for (int i = 1; i < node->window_count; ++i) {
        if (node->window_list[i] == window->id) {
            return window_manager_find_window(wm, node->window_list[i-1]);
        }
    }

    return NULL;
}

struct window *window_manager_find_next_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    for (int i = 0; i < node->window_count - 1; ++i) {
        if (node->window_list[i] == window->id) {
            return window_manager_find_window(wm, node->window_list[i+1]);
        }
    }

    return NULL;
}

struct window *window_manager_find_first_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 ? window_manager_find_window(wm, node->window_list[0]) : NULL;
}

struct window *window_manager_find_last_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 ? window_manager_find_window(wm, node->window_list[node->window_count-1]) : NULL;
}

struct window *window_manager_find_recent_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 ? window_manager_find_window(wm, node->window_order[1]) : NULL;
}

struct window *window_manager_find_window_in_stack(struct space_manager *sm, struct window_manager *wm, struct window *window, int index)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    return node->window_count > 1 && in_range_ii(index, 1, node->window_count) ? window_manager_find_window(wm, node->window_list[index-1]) : NULL;
}

struct window *window_manager_find_largest_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    uint32_t best_id   = 0;
    uint32_t best_area = 0;

    for (struct window_node *node = window_node_find_first_leaf(view->root); node != NULL; node = window_node_find_next_leaf(node)) {
        uint32_t area = node->area.w * node->area.h;
        if (area > best_area) {
            best_id   = node->window_order[0];
            best_area = area;
        }
    }

    return best_id ? window_manager_find_window(wm, best_id) : NULL;
}

struct window *window_manager_find_smallest_managed_window(struct space_manager *sm, struct window_manager *wm)
{
    struct view *view = space_manager_find_view(sm, space_manager_active_space());
    if (!view) return NULL;

    uint32_t best_id   = 0;
    uint32_t best_area = UINT32_MAX;

    for (struct window_node *node = window_node_find_first_leaf(view->root); node != NULL; node = window_node_find_next_leaf(node)) {
        uint32_t area = node->area.w * node->area.h;
        if (area <= best_area) {
            best_id   = node->window_order[0];
            best_area = area;
        }
    }

    return best_id ? window_manager_find_window(wm, best_id) : NULL;
}

struct window *window_manager_find_sibling_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *sibling_node = window_node_is_left_child(node) ? node->parent->right : node->parent->left;
    if (!window_node_is_leaf(sibling_node)) return NULL;

    return window_manager_find_window(wm, sibling_node->window_order[0]);
}

struct window *window_manager_find_first_nephew_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *sibling_node = window_node_is_left_child(node) ? node->parent->right : node->parent->left;
    if (window_node_is_leaf(sibling_node) || !window_node_is_leaf(sibling_node->left)) return NULL;

    return window_manager_find_window(wm, sibling_node->left->window_order[0]);
}

struct window *window_manager_find_second_nephew_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *sibling_node = window_node_is_left_child(node) ? node->parent->right : node->parent->left;
    if (window_node_is_leaf(sibling_node) || !window_node_is_leaf(sibling_node->right)) return NULL;

    return window_manager_find_window(wm, sibling_node->right->window_order[0]);
}

struct window *window_manager_find_uncle_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *grandparent = node->parent->parent;
    if (!grandparent) return NULL;

    struct window_node *uncle_node = window_node_is_left_child(node->parent) ? grandparent->right : grandparent->left;
    if (!window_node_is_leaf(uncle_node)) return NULL;

    return window_manager_find_window(wm, uncle_node->window_order[0]);
}

struct window *window_manager_find_first_cousin_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *grandparent = node->parent->parent;
    if (!grandparent) return NULL;

    struct window_node *uncle_node = window_node_is_left_child(node->parent) ? grandparent->right : grandparent->left;
    if (window_node_is_leaf(uncle_node) || !window_node_is_leaf(uncle_node->left)) return NULL;

    return window_manager_find_window(wm, uncle_node->left->window_order[0]);
}

struct window *window_manager_find_second_cousin_for_managed_window(struct window_manager *wm, struct window *window)
{
    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view) return NULL;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node || !node->parent) return NULL;

    struct window_node *grandparent = node->parent->parent;
    if (!grandparent) return NULL;

    struct window_node *uncle_node = window_node_is_left_child(node->parent) ? grandparent->right : grandparent->left;
    if (window_node_is_leaf(uncle_node) || !window_node_is_leaf(uncle_node->right)) return NULL;

    return window_manager_find_window(wm, uncle_node->right->window_order[0]);
}

static void window_manager_make_key_window(ProcessSerialNumber *window_psn, uint32_t window_id)
{
    //
    // :SynthesizedEvent
    //
    // NOTE(asmvik): These events will be picked up by an event-tap
    // registered at the "Annotated Session" location; specifying that an
    // event-tap is placed at the point where session events have been
    // annotated to flow to an application.
    //

    memset(g_event_bytes, 0, 0xf8);
    g_event_bytes[0x04] = 0xf8;
    g_event_bytes[0x3a] = 0x10;
    memcpy(g_event_bytes + 0x3c, &window_id, sizeof(uint32_t));
    memset(g_event_bytes + 0x20, 0xff, 0x10);

    g_event_bytes[0x08] = 0x01;
    SLPSPostEventRecordTo(window_psn, g_event_bytes);

    g_event_bytes[0x08] = 0x02;
    SLPSPostEventRecordTo(window_psn, g_event_bytes);
}

void window_manager_focus_window_without_raise(ProcessSerialNumber *window_psn, uint32_t window_id)
{
    TIME_FUNCTION;

    if (psn_equals(window_psn, &g_window_manager.focused_window_psn)) {
        memset(g_event_bytes, 0, 0xf8);
        g_event_bytes[0x04] = 0xf8;
        g_event_bytes[0x08] = 0x0d;

        g_event_bytes[0x8a] = 0x02;
        memcpy(g_event_bytes + 0x3c, &g_window_manager.focused_window_id, sizeof(uint32_t));
        SLPSPostEventRecordTo(&g_window_manager.focused_window_psn, g_event_bytes);

        //
        // @hack
        // Artificially delay the activation by 40ms. This is necessary
        // because some applications appear to be confused if both of
        // the events appear instantaneously.
        //

        usleep(40000);

        g_event_bytes[0x8a] = 0x01;
        memcpy(g_event_bytes + 0x3c, &window_id, sizeof(uint32_t));
        SLPSPostEventRecordTo(window_psn, g_event_bytes);
    }

    _SLPSSetFrontProcessWithOptions(window_psn, window_id, kCPSUserGenerated);
    window_manager_make_key_window(window_psn, window_id);
}

void window_manager_focus_window_with_raise(ProcessSerialNumber *window_psn, uint32_t window_id, AXUIElementRef window_ref)
{
    TIME_FUNCTION;

#if 1
    _SLPSSetFrontProcessWithOptions(window_psn, window_id, kCPSUserGenerated);
    window_manager_make_key_window(window_psn, window_id);
    AXUIElementPerformAction(window_ref, kAXRaiseAction);
#else
    scripting_addition_focus_window(window_id);
#endif
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
struct application *window_manager_focused_application(struct window_manager *wm)
{
    TIME_FUNCTION;

    ProcessSerialNumber psn = {0};
    _SLPSGetFrontProcess(&psn);

    pid_t pid;
    GetProcessPID(&psn, &pid);

    return window_manager_find_application(wm, pid);
}

struct window *window_manager_focused_window(struct window_manager *wm)
{
    TIME_FUNCTION;

    struct application *application = window_manager_focused_application(wm);
    if (!application) return NULL;

    uint32_t window_id = application_focused_window(application);
    return window_manager_find_window(wm, window_id);
}
#pragma clang diagnostic pop

bool window_manager_find_lost_front_switched_event(struct window_manager *wm, pid_t pid)
{
    return table_find(&wm->application_lost_front_switched_event, &pid) != NULL;
}

void window_manager_remove_lost_front_switched_event(struct window_manager *wm, pid_t pid)
{
    table_remove(&wm->application_lost_front_switched_event, &pid);
}

void window_manager_add_lost_front_switched_event(struct window_manager *wm, pid_t pid)
{
    table_add(&wm->application_lost_front_switched_event, &pid, (void *)(intptr_t) 1);
}

bool window_manager_find_lost_focused_event(struct window_manager *wm, uint32_t window_id)
{
    return table_find(&wm->window_lost_focused_event, &window_id) != NULL;
}

void window_manager_remove_lost_focused_event(struct window_manager *wm, uint32_t window_id)
{
    table_remove(&wm->window_lost_focused_event, &window_id);
}

void window_manager_add_lost_focused_event(struct window_manager *wm, uint32_t window_id)
{
    table_add(&wm->window_lost_focused_event, &window_id, (void *)(intptr_t) 1);
}

struct window *window_manager_find_window(struct window_manager *wm, uint32_t window_id)
{
    return table_find(&wm->window, &window_id);
}

void window_manager_remove_window(struct window_manager *wm, uint32_t window_id)
{
    table_remove(&wm->window, &window_id);
}

void window_manager_add_window(struct window_manager *wm, struct window *window)
{
    table_add(&wm->window, &window->id, window);
}

struct application *window_manager_find_application(struct window_manager *wm, pid_t pid)
{
    return table_find(&wm->application, &pid);
}

void window_manager_remove_application(struct window_manager *wm, pid_t pid)
{
    table_remove(&wm->application, &pid);
}

void window_manager_add_application(struct window_manager *wm, struct application *application)
{
    table_add(&wm->application, &application->pid, application);
}

struct window **window_manager_find_application_windows(struct window_manager *wm, struct application *application, int *window_count)
{
    *window_count = 0;
    struct window **window_list = ts_alloc_list(struct window *, wm->window.count);

    table_for (struct window *window, wm->window, {
        if (window->application == application) {
            window_list[(*window_count)++] = window;
        }
    })

    return window_list;
}

struct window *window_manager_create_and_add_window(struct space_manager *sm, struct window_manager *wm, struct application *application, AXUIElementRef window_ref, uint32_t window_id, bool one_shot_rules)
{
    struct window *window = window_create(application, window_ref, window_id);

    char *window_title = window_title_ts(window);
    char *window_role = window_role_ts(window);
    char *window_subrole = window_subrole_ts(window);
    debug("%s:%d %s - %s (%s:%s:%d)\n", __FUNCTION__, window->id, window->application->name, window_title, window_role, window_subrole, window->is_root);

    if (window_is_unknown(window)) {
        debug("%s: ignoring AXUnknown window %s %d\n", __FUNCTION__, window->application->name, window->id);
        window_manager_remove_lost_focused_event(wm, window->id);
        window_destroy(window);
        return NULL;
    }

    //
    // NOTE(asmvik): Attempt to track **all** windows.
    //

    if (!window_observe(window)) {
        debug("%s: could not observe %s %d\n", __FUNCTION__, window->application->name, window->id);
        window_manager_remove_lost_focused_event(wm, window->id);
        window_unobserve(window);
        window_destroy(window);
        return NULL;
    }

    if (window_manager_find_lost_focused_event(wm, window->id)) {
        event_loop_post(&g_event_loop, WINDOW_FOCUSED, (void *)(intptr_t) window->id, 0);
        window_manager_remove_lost_focused_event(wm, window->id);
    }

    window_manager_add_window(wm, window);

    //
    // NOTE(asmvik): However, only **root windows** are eligible for management.
    //

    if (window->is_root) {

        //
        // NOTE(asmvik): A lot of windows misreport their accessibility role, so we allow the user
        // to specify rules to make sure that we do in fact manage these windows properly.
        //
        // This part of the rule must be applied at this stage (prior to other rule properties), and if
        // no such rule matches this window, it will be ignored if it does not have a role of kAXWindowRole.
        //

        window_manager_apply_manage_rules_to_window(sm, wm, window, window_title, window_role, window_subrole, one_shot_rules);

        if (window_manager_is_window_eligible(window)) {
            window->is_eligible = true;
            window_manager_apply_rules_to_window(sm, wm, window, window_title, window_role, window_subrole, one_shot_rules);

            if (application->is_hidden)                              goto out;
            if (window_check_flag(window, WINDOW_MINIMIZE))          goto out;
            if (window_check_flag(window, WINDOW_FULLSCREEN))        goto out;
            if (window_check_rule_flag(window, WINDOW_RULE_MANAGED)) goto out;

            if (window_check_rule_flag(window, WINDOW_RULE_FULLSCREEN)) {
                window_clear_rule_flag(window, WINDOW_RULE_FULLSCREEN);
                goto out;
            }

            if (window_is_sticky(window->id) ||
                !window_can_move(window) ||
                !window_is_standard(window) ||
                !window_level_is_standard(window) ||
                (!window_can_resize(window) && window_is_undersized(window))) {
                window_set_flag(window, WINDOW_FLOAT);
            }
        } else {
            debug("%s ignoring incorrectly marked window %s %d\n", __FUNCTION__, window->application->name, window->id);
            window_set_flag(window, WINDOW_FLOAT);

            //
            // NOTE(asmvik): Print window information when debug_output is enabled.
            // Useful for identifying and creating rules if this window should in fact be managed.
            //

            if (g_verbose) {
                fprintf(stdout, "window info: \n");
                window_serialize(stdout, window, 0);
                fprintf(stdout, "\n");
            }
        }
    } else {
        debug("%s ignoring child window %s %d\n", __FUNCTION__, window->application->name, window->id);
        window_set_flag(window, WINDOW_FLOAT);

        //
        // NOTE(asmvik): Print window information when debug_output is enabled.
        //

        if (g_verbose) {
            fprintf(stdout, "window info: \n");
            window_serialize(stdout, window, 0);
            fprintf(stdout, "\n");
        }
    }

out:
    return window;
}

struct window **window_manager_add_application_windows(struct space_manager *sm, struct window_manager *wm, struct application *application, int *count)
{
    *count = 0;
    CFArrayRef window_list = application_window_list(application);
    if (!window_list) return NULL;

    int window_count = CFArrayGetCount(window_list);
    struct window **list = ts_alloc_list(struct window *, window_count);

    for (int i = 0; i < window_count; ++i) {
        AXUIElementRef window_ref = CFArrayGetValueAtIndex(window_list, i);

        uint32_t window_id = ax_window_id(window_ref);
        if (!window_id || window_manager_find_window(wm, window_id)) continue;

        struct window *window = window_manager_create_and_add_window(sm, wm, application, CFRetain(window_ref), window_id, true);
        if (window) list[(*count)++] = window;
    }

    int rule_len = buf_len(wm->rules);
    for (int i = 0; i < rule_len; ++i) {
        if (rule_check_flag(&wm->rules[i], RULE_ONE_SHOT_REMOVE)) {
            rule_destroy(&wm->rules[i]);
            if (buf_del(wm->rules, i)) {
                --i;
                --rule_len;
            }
        }
    }

    CFRelease(window_list);
    return list;
}

static uint32_t *window_manager_existing_application_window_list(struct application *application, int *window_count)
{
    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return NULL;

    int space_count = 0;
    uint64_t *space_list = NULL;

    for (int i = 0; i < display_count; ++i) {
        int count;
        uint64_t *list = display_space_list(display_list[i], &count);
        if (!list) continue;

        //
        // NOTE(asmvik): display_space_list(..) uses a linear allocator,
        // and so we only need to track the beginning of the first list along
        // with the total number of windows that have been allocated.
        //

        if (!space_list) space_list = list;
        space_count += count;
    }

    return space_list ? space_window_list_for_connection(space_list, space_count, application ? application->connection : 0, window_count, true) : NULL;
}

bool window_manager_add_existing_application_windows(struct space_manager *sm, struct window_manager *wm, struct application *application, int refresh_index)
{
    bool result = false;

    int global_window_count;
    uint32_t *global_window_list = window_manager_existing_application_window_list(application, &global_window_count);
    if (!global_window_list) return result;

    CFArrayRef window_list_ref = application_window_list(application);
    int window_count = window_list_ref ? CFArrayGetCount(window_list_ref) : 0;

    int empty_count = 0;
    for (int i = 0; i < window_count; ++i) {
        AXUIElementRef window_ref = CFArrayGetValueAtIndex(window_list_ref, i);
        uint32_t window_id = ax_window_id(window_ref);

        //
        // @cleanup
        //
        // :Workaround
        //
        // NOTE(asmvik): The AX API appears to always include a single element for Finder that returns an empty window id.
        // This is likely the desktop window. Other similar cases should be handled the same way; simply ignore the window when
        // we attempt to do an equality check to see if we have correctly discovered the number of windows to track.
        //

        if (!window_id) {
            ++empty_count;
            continue;
        }

        if (!window_manager_find_window(wm, window_id)) {
            window_manager_create_and_add_window(sm, wm, application, CFRetain(window_ref), window_id, false);
        }
    }

    if (global_window_count != window_count-empty_count) {
        if (refresh_index == -1) {
            bool missing_window = false;
            uint32_t *app_window_list = NULL;

            for (int i = 0; i < global_window_count; ++i) {
                struct window *window = window_manager_find_window(wm, global_window_list[i]);
                if (!window) {
                    missing_window = true;
                    ts_buf_push(app_window_list, global_window_list[i]);
                }
            }

            if (missing_window) {
                debug("%s: %s has %d windows that are not yet resolved, attempting workaround\n", __FUNCTION__, application->name, ts_buf_len(app_window_list));

                //
                // NOTE(asmvik): MacOS API does not return AXUIElementRef of windows on inactive spaces.
                // However, we can just brute-force the element_id and create the AXUIElementRef ourselves.
                //
                // :Attribution
                // https://github.com/decodism
                // https://github.com/lwouis/alt-tab-macos/issues/1324#issuecomment-2631035482
                //

                CFMutableDataRef data_ref = CFDataCreateMutable(NULL, 0x14);
                CFDataIncreaseLength(data_ref, 0x14);

                uint8_t *data = CFDataGetMutableBytePtr(data_ref);
                *(uint32_t *) (data + 0x0) = application->pid;
                *(uint32_t *) (data + 0x8) = 0x636f636f;

                for (uint64_t element_id = 0; element_id < 0x7fff; ++element_id) {
                    int app_window_list_len = ts_buf_len(app_window_list);
                    if (app_window_list_len == 0) break;

                    memcpy(data+0xc, &element_id, sizeof(uint64_t));
                    AXUIElementRef element_ref = _AXUIElementCreateWithRemoteToken(data_ref);

                    const void *role = NULL;
                    AXUIElementCopyAttributeValue(element_ref, kAXRoleAttribute, &role);

                    if (role) {
                        if (CFEqual(role, kAXWindowRole)) {
                            uint32_t element_wid = ax_window_id(element_ref);
                            bool matched = false;

                            if (element_wid != 0) {
                                for (int i = 0; i < app_window_list_len; ++i) {
                                    if (app_window_list[i] == element_wid) {
                                        matched = true;
                                        ts_buf_del(app_window_list, i);
                                        break;
                                    }
                                }
                            }

                            if (matched) {
                                window_manager_create_and_add_window(sm, wm, application, element_ref, element_wid, false);
                            } else {
                                CFRelease(element_ref);
                            }
                        }

                        CFRelease(role);
                    }
                }

                CFRelease(data_ref);
            }

            if (ts_buf_len(app_window_list) > 0) {
                debug("%s: workaround failed to resolve all windows for %s\n", __FUNCTION__, application->name);
                buf_push(wm->applications_to_refresh, application);
            } else {
                debug("%s: workaround resolved all windows for %s\n", __FUNCTION__, application->name);
            }
        } else {
            bool missing_window = false;

            for (int i = 0; i < global_window_count; ++i) {
                struct window *window = window_manager_find_window(wm, global_window_list[i]);
                if (!window) {
                    missing_window = true;
                    break;
                }
            }

            if (!missing_window) {
                debug("%s: all windows for %s are now resolved\n", __FUNCTION__, application->name);
                buf_del(wm->applications_to_refresh, refresh_index);
                result = true;
            }
        }
    } else if (refresh_index != -1) {
        debug("%s: all windows for %s are now resolved\n", __FUNCTION__, application->name);
        buf_del(wm->applications_to_refresh, refresh_index);
        result = true;
    }

    if (window_list_ref) CFRelease(window_list_ref);

    return result;
}

enum window_op_error window_manager_set_window_insertion(struct space_manager *sm, struct window *window, int direction)
{
    TIME_FUNCTION;

    uint64_t sid = window_space(window->id);
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    if (view->insertion_point && view->insertion_point != window->id) {
        struct window_node *insert_node = view_find_window_node(view, view->insertion_point);
        if (insert_node) {
            insert_feedback_destroy(insert_node);
            insert_node->split = SPLIT_NONE;
            insert_node->child = CHILD_NONE;
            insert_node->insert_dir = 0;
        }
    }

    if (direction == node->insert_dir) {
        insert_feedback_destroy(node);
        node->split = SPLIT_NONE;
        node->child = CHILD_NONE;
        node->insert_dir = 0;
        view->insertion_point = 0;
        return WINDOW_OP_ERROR_SUCCESS;
    }

    if (direction == DIR_NORTH) {
        node->split = SPLIT_X;
        node->child = CHILD_FIRST;
    } else if (direction == DIR_EAST) {
        node->split = SPLIT_Y;
        node->child = CHILD_SECOND;
    } else if (direction == DIR_SOUTH) {
        node->split = SPLIT_X;
        node->child = CHILD_SECOND;
    } else if (direction == DIR_WEST) {
        node->split = SPLIT_Y;
        node->child = CHILD_FIRST;
    }

    node->insert_dir = direction;
    view->insertion_point = node->window_order[0];
    insert_feedback_show(node);

    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_stack_window(struct window_manager *wm, struct window *a, struct window *b)
{
    TIME_FUNCTION;

    if (a->id == b->id) return WINDOW_OP_ERROR_SAME_WINDOW;

    struct view *a_view = window_manager_find_managed_window(wm, a);
    if (!a_view) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    struct view *b_view = window_manager_find_managed_window(wm, b);
    if (b_view) {
        space_manager_untile_window(b_view, b);
        window_manager_remove_managed_window(wm, b->id);
    } else if (window_check_flag(b, WINDOW_FLOAT)) {
        if (!window_manager_is_window_eligible(b)) return WINDOW_OP_ERROR_INVALID_SRC_NODE;
        window_clear_flag(b, WINDOW_FLOAT);
    }

    struct window_node *a_node = view_find_window_node(a_view, a->id);
    if (a_node->window_count+1 >= NODE_MAX_WINDOW_COUNT) return WINDOW_OP_ERROR_MAX_STACK;

    view_stack_window_node(a_node, b);
    window_manager_add_managed_window(wm, b, a_view);

    struct area area = a_node->zoom ? a_node->zoom->area : a_node->area;
    window_manager_animate_window((struct window_capture) { b, area.x, area.y, area.w, area.h });
    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_warp_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b)
{
    TIME_FUNCTION;

    if (a->id == b->id) return WINDOW_OP_ERROR_SAME_WINDOW;

    uint64_t a_sid = window_space(a->id);
    struct view *a_view = space_manager_find_view(sm, a_sid);
    if (a_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    uint64_t b_sid = window_space(b->id);
    struct view *b_view = space_manager_find_view(sm, b_sid);
    if (b_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_DST_VIEW;

    struct window_node *a_node = view_find_window_node(a_view, a->id);
    if (!a_node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    struct window_node *b_node = view_find_window_node(b_view, b->id);
    if (!b_node) return WINDOW_OP_ERROR_INVALID_DST_NODE;

    if (a_node == b_node) return WINDOW_OP_ERROR_SAME_STACK;

    if (a_node->parent && b_node->parent &&
        a_node->parent == b_node->parent &&
        a_node->window_count == 1) {
        if (window_node_contains_window(b_node, b_view->insertion_point)) {
            b_node->parent->split = b_node->split;
            b_node->parent->child = b_node->child;

            view_remove_window_node(a_view, a);
            window_manager_remove_managed_window(wm, a->id);
            window_manager_add_managed_window(wm, a, b_view);
            struct window_node *a_node_add = view_add_window_node_with_insertion_point(b_view, a, b->id);

            struct window_capture *window_list = NULL;
            window_node_capture_windows(a_node_add, &window_list);
            window_manager_animate_window_list(window_list, ts_buf_len(window_list));
        } else {
            if (window_node_contains_window(a_node, a_view->insertion_point)) {
                a_view->insertion_point = b->id;
            }

            window_node_swap_window_list(a_node, b_node);

            struct window_capture *window_list = NULL;
            window_node_capture_windows(a_node, &window_list);
            window_node_capture_windows(b_node, &window_list);
            window_manager_animate_window_list(window_list, ts_buf_len(window_list));
        }
    } else {
        if (a_view->sid == b_view->sid) {

            //
            // :NaturalWarp
            //
            // NOTE(asmvik): Precalculate both target areas and select the one that has the closest distance to the source area.
            // This allows the warp to feel more natural in terms of where the window is placed on screen, however, this is only utilized
            // for warp operations where both operands belong to the same space. There may be a better system to handle this if/when multiple
            // monitors should be supported.
            //

            struct area cf, cs;
            area_make_pair(window_node_get_split(b_view, b_node), window_node_get_gap(b_view), window_node_get_ratio(b_node), &b_node->area, &cf, &cs);

            CGPoint ca = { (int)(0.5f + a_node->area.x + a_node->area.w / 2.0f), (int)(0.5f + a_node->area.y + a_node->area.h / 2.0f) };
            float dcf = powf((ca.x - (int)(0.5f + cf.x + cf.w / 2.0f)), 2.0f) + powf((ca.y - (int)(0.5f + cf.y + cf.h / 2.0f)), 2.0f);
            float dcs = powf((ca.x - (int)(0.5f + cs.x + cs.w / 2.0f)), 2.0f) + powf((ca.y - (int)(0.5f + cs.y + cs.h / 2.0f)), 2.0f);

            if (dcf < dcs) {
                b_node->child = CHILD_FIRST;
            } else if (dcf > dcs) {
                b_node->child = CHILD_SECOND;
            } else {
                b_node->child = window_node_is_left_child(a_node) ? CHILD_FIRST : CHILD_SECOND;
            }

            struct window_node *a_node_rm = view_remove_window_node(a_view, a);
            struct window_node *a_node_add = view_add_window_node_with_insertion_point(b_view, a, b->id);

            struct window_capture *window_list = NULL;
            if (a_node_rm) {
                window_node_capture_windows(a_node_rm, &window_list);
            }

            if (a_node_rm != a_node_add && a_node_rm != a_node_add->parent) {
                window_node_capture_windows(a_node_add, &window_list);
            }

            window_manager_animate_window_list(window_list, ts_buf_len(window_list));
        } else {
            if (wm->focused_window_id == a->id) {
                struct window *next = window_manager_find_window_on_space_by_rank_filtering_window(wm, a_view->sid, 1, a->id);
                if (next) {
                    window_manager_focus_window_with_raise(&next->application->psn, next->id, next->ref);
                } else {
                    _SLPSSetFrontProcessWithOptions(&g_process_manager.finder_psn, 0, kCPSNoWindows);
                }
            }

            //
            // :NaturalWarp
            //
            // TODO(asmvik): Warp operations with operands that belong to different monitors does not yet implement a heuristic to select
            // the target area that feels the most natural in terms of where the window is placed on screen. Is it possible to do better when
            // warping between spaces that belong to the same monitor as well??
            //

            space_manager_untile_window(a_view, a);
            window_manager_remove_managed_window(wm, a->id);
            window_manager_add_managed_window(wm, a, b_view);
            space_manager_move_window_to_space(b_view->sid, a);
            space_manager_tile_window_on_space_with_insertion_point(sm, a, b_view->sid, b->id);
        }
    }

    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_swap_window(struct space_manager *sm, struct window_manager *wm, struct window *a, struct window *b)
{
    TIME_FUNCTION;

    if (a->id == b->id) return WINDOW_OP_ERROR_SAME_WINDOW;

    uint64_t a_sid = window_space(a->id);
    struct view *a_view = space_manager_find_view(sm, a_sid);

    uint64_t b_sid = window_space(b->id);
    struct view *b_view = space_manager_find_view(sm, b_sid);

    struct window_node *a_node = view_find_window_node(a_view, a->id);
    if (!a_node) return WINDOW_OP_ERROR_INVALID_SRC_NODE;

    struct window_node *b_node = view_find_window_node(b_view, b->id);
    if (!b_node) return WINDOW_OP_ERROR_INVALID_DST_NODE;

    if (a_node == b_node) {
        int a_list_index = 0;
        int a_order_index = 0;

        int b_list_index = 0;
        int b_order_index = 0;

        for (int i = 0; i < a_node->window_count; ++i) {
            if (a_node->window_list[i] == a->id) {
                a_list_index = i;
            } else if (a_node->window_list[i] == b->id) {
                b_list_index = i;
            }

            if (a_node->window_order[i] == a->id) {
                a_order_index = i;
            } else if (a_node->window_order[i] == b->id) {
                b_order_index = i;
            }
        }

        a_node->window_list[a_list_index] = b->id;
        a_node->window_order[a_order_index] = b->id;

        a_node->window_list[b_list_index] = a->id;
        a_node->window_order[b_order_index] = a->id;

        if (a->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&b->application->psn, b->id, b->ref);
        } else if (b->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&a->application->psn, a->id, a->ref);
        }

        return WINDOW_OP_ERROR_SUCCESS;
    }

    if (a_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;
    if (b_view->layout != VIEW_BSP) return WINDOW_OP_ERROR_INVALID_DST_VIEW;

    if (window_node_contains_window(a_node, a_view->insertion_point)) {
        a_view->insertion_point = b->id;
    } else if (window_node_contains_window(b_node, b_view->insertion_point)) {
        b_view->insertion_point = a->id;
    }

    bool a_visible = space_is_visible(a_view->sid);
    bool b_visible = space_is_visible(b_view->sid);

    if (a_view->sid != b_view->sid) {
        for (int i = 0; i < a_node->window_count; ++i) {
            struct window *window = window_manager_find_window(wm, a_node->window_list[i]);
            window_manager_remove_managed_window(wm, a_node->window_list[i]);
            space_manager_move_window_to_space(b_view->sid, window);
            window_manager_add_managed_window(wm, window, b_view);
        }

        for (int i = 0; i < b_node->window_count; ++i) {
            struct window *window = window_manager_find_window(wm, b_node->window_list[i]);
            window_manager_remove_managed_window(wm, b_node->window_list[i]);
            space_manager_move_window_to_space(a_view->sid, window);
            window_manager_add_managed_window(wm, window, a_view);
        }

        if (a_visible && !b_visible && a->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&b->application->psn, b->id, b->ref);
        } else if (b_visible && !a_visible && b->id == wm->focused_window_id) {
            window_manager_focus_window_with_raise(&a->application->psn, a->id, a->ref);
        }
    }

    window_node_swap_window_list(a_node, b_node);
    struct window_capture *window_list = NULL;

    if (a_visible) {
        window_node_capture_windows(a_node, &window_list);
    } else {
        view_set_flag(a_view, VIEW_IS_DIRTY);
    }

    if (b_visible) {
        window_node_capture_windows(b_node, &window_list);
    } else {
        view_set_flag(b_view, VIEW_IS_DIRTY);
    }

    window_manager_animate_window_list(window_list, ts_buf_len(window_list));
    return WINDOW_OP_ERROR_SUCCESS;
}

enum window_op_error window_manager_minimize_window(struct window *window)
{
    TIME_FUNCTION;

    if (!window_can_minimize(window)) return WINDOW_OP_ERROR_CANT_MINIMIZE;
    if (window_check_flag(window, WINDOW_MINIMIZE)) return WINDOW_OP_ERROR_ALREADY_MINIMIZED;

    AXError result = AXUIElementSetAttributeValue(window->ref, kAXMinimizedAttribute, kCFBooleanTrue);
    return result == kAXErrorSuccess ? WINDOW_OP_ERROR_SUCCESS : WINDOW_OP_ERROR_MINIMIZE_FAILED;
}

enum window_op_error window_manager_deminimize_window(struct window *window)
{
    TIME_FUNCTION;

    if (!window_check_flag(window, WINDOW_MINIMIZE)) return WINDOW_OP_ERROR_NOT_MINIMIZED;

    AXError result = AXUIElementSetAttributeValue(window->ref, kAXMinimizedAttribute, kCFBooleanFalse);
    return result == kAXErrorSuccess ? WINDOW_OP_ERROR_SUCCESS : WINDOW_OP_ERROR_DEMINIMIZE_FAILED;
}

bool window_manager_close_window(struct window *window)
{
    TIME_FUNCTION;

    CFTypeRef button = NULL;
    AXUIElementCopyAttributeValue(window->ref, kAXCloseButtonAttribute, &button);
    if (!button) return false;

    AXUIElementPerformAction(button, kAXPressAction);
    CFRelease(button);

    return true;
}

void window_manager_send_window_to_space(struct space_manager *sm, struct window_manager *wm, struct window *window, uint64_t dst_sid, bool moved_by_rule)
{
    TIME_FUNCTION;

    uint64_t src_sid = window_space(window->id);
    if (src_sid == dst_sid) return;

    if ((space_is_visible(src_sid) && (moved_by_rule || wm->focused_window_id == window->id))) {
        struct window *next = window_manager_find_window_on_space_by_rank_filtering_window(wm, src_sid, 1, window->id);
        if (next) {
            window_manager_focus_window_with_raise(&next->application->psn, next->id, next->ref);
        } else {
            _SLPSSetFrontProcessWithOptions(&g_process_manager.finder_psn, 0, kCPSNoWindows);
        }
    }

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(wm, window->id);
    }

    space_manager_move_window_to_space(dst_sid, window);
    SLSSpaceSetFrontPSN(g_connection, dst_sid, window->application->psn);

    if (window_manager_should_manage_window(window)) {
        struct view *view = space_manager_tile_window_on_space(sm, window, dst_sid);
        window_manager_add_managed_window(wm, window, view);
    }
}

enum window_op_error window_manager_apply_grid(struct space_manager *sm, struct window_manager *wm, struct window *window, unsigned r, unsigned c, unsigned x, unsigned y, unsigned w, unsigned h)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (view) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    uint32_t did = window_display_id(window->id);
    if (!did) return WINDOW_OP_ERROR_INVALID_SRC_VIEW;

    if (x >=   c) x = c - 1;
    if (y >=   r) y = r - 1;
    if (w <=   0) w = 1;
    if (h <=   0) h = 1;
    if (w >  c-x) w = c - x;
    if (h >  r-y) h = r - y;

    CGRect bounds = display_bounds_constrained(did, false);
    struct view *dview = space_manager_find_view(sm, display_space_id(did));

    if (dview) {
        if (view_check_flag(dview, VIEW_ENABLE_PADDING)) {
            bounds.origin.x    += dview->left_padding;
            bounds.size.width  -= (dview->left_padding + dview->right_padding);
            bounds.origin.y    += dview->top_padding;
            bounds.size.height -= (dview->top_padding + dview->bottom_padding);
        }

        if (view_check_flag(dview, VIEW_ENABLE_GAP)) {
            int gap = window_node_get_gap(dview);

            if (x > 0) {
                bounds.origin.x   += gap;
                bounds.size.width -= gap;
            }

            if (y > 0) {
                bounds.origin.y    += gap;
                bounds.size.height -= gap;
            }

            if (c > x+w) bounds.size.width  -= gap;
            if (r > y+h) bounds.size.height -= gap;
        }
    }

    float cw = bounds.size.width / c;
    float ch = bounds.size.height / r;
    float fx = bounds.origin.x + bounds.size.width  - cw * (c - x);
    float fy = bounds.origin.y + bounds.size.height - ch * (r - y);
    float fw = cw * w;
    float fh = ch * h;

    window_manager_animate_window((struct window_capture) { .window = window, .x = fx, .y = fy, .w = fw, .h = fh });
    return WINDOW_OP_ERROR_SUCCESS;
}

void window_manager_make_window_floating(struct space_manager *sm, struct window_manager *wm, struct window *window, bool should_float, bool force)
{
    TIME_FUNCTION;

    if (!window_manager_is_window_eligible(window)) return;

    if (!force) {
        if (!window_is_standard(window) || !window_level_is_standard(window) || !window_can_move(window)) {
            if (!window_check_rule_flag(window, WINDOW_RULE_MANAGED)) {
                return;
            }
        }
    }

    if (should_float) {
        struct view *view = window_manager_find_managed_window(wm, window);
        if (view) {
            space_manager_untile_window(view, window);
            window_manager_remove_managed_window(wm, window->id);
        }
        window_set_flag(window, WINDOW_FLOAT);
    } else {
        window_clear_flag(window, WINDOW_FLOAT);

        if (!window_check_flag(window, WINDOW_STICKY)) {
            if ((window_manager_should_manage_window(window)) && (!window_manager_find_managed_window(wm, window))) {
                struct view *view = space_manager_tile_window_on_space(sm, window, space_manager_active_space());
                window_manager_add_managed_window(wm, window, view);
            }
        }
    }
}

void window_manager_wait_for_native_fullscreen_transition(struct window *window)
{
    TIME_FUNCTION;

    if (workspace_is_macos_monterey() ||
        workspace_is_macos_ventura() ||
        workspace_is_macos_sonoma() ||
        workspace_is_macos_sequoia() ||
        workspace_is_macos_tahoe()) {
        while (!space_is_user(space_manager_active_space())) {

            //
            // NOTE(asmvik): Window has exited native-fullscreen mode.
            // We need to spin lock until the display is finished animating
            // because we are not actually able to interact with the window.
            //
            // The display_manager API does not work on macOS Monterey.
            //

            usleep(100000);
        }
    } else {
        uint32_t did = window_display_id(window->id);

        do {

            //
            // NOTE(asmvik): Window has exited native-fullscreen mode.
            // We need to spin lock until the display is finished animating
            // because we are not actually able to interact with the window.
            //

            usleep(100000);
        } while (display_manager_display_is_animating(did));
    }
}

void window_manager_toggle_window_native_fullscreen(struct window *window)
{
    TIME_FUNCTION;

    uint32_t sid = window_space(window->id);

    //
    // NOTE(asmvik): The window must become the focused window
    // before we can change its fullscreen attribute. We focus the
    // window and spin lock until a potential space animation has finished.
    //

    window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    while (sid != space_manager_active_space()) { usleep(100000); }


    if (!window_is_fullscreen(window)) {
        AXUIElementSetAttributeValue(window->ref, kAXFullscreenAttribute, kCFBooleanTrue);
    } else {
        AXUIElementSetAttributeValue(window->ref, kAXFullscreenAttribute, kCFBooleanFalse);
    }

    //
    // NOTE(asmvik): We toggled the fullscreen attribute and must
    // now spin lock until the post-exit space animation has finished.
    //

    window_manager_wait_for_native_fullscreen_transition(window);
}

void window_manager_toggle_window_zoom_parent(struct window_manager *wm, struct window *window)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view || view->layout != VIEW_BSP) return;

    struct window_node *node = view_find_window_node(view, window->id);
    assert(node);

    if (!node->parent) return;

    if (node->zoom == node->parent) {
        node->zoom = NULL;
        if (space_is_visible(view->sid)) {
            window_node_flush(node);
        } else {
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    } else {
        node->zoom = node->parent;
        if (space_is_visible(view->sid)) {
            window_node_flush(node);
        } else {
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    }
}

void window_manager_toggle_window_zoom_fullscreen(struct window_manager *wm, struct window *window)
{
    TIME_FUNCTION;

    struct view *view = window_manager_find_managed_window(wm, window);
    if (!view || view->layout != VIEW_BSP) return;

    struct window_node *node = view_find_window_node(view, window->id);
    assert(node);

    if (node == view->root) return;

    if (node->zoom == view->root) {
        node->zoom = NULL;
        if (space_is_visible(view->sid)) {
            window_node_flush(node);
        } else {
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    } else {
        node->zoom = view->root;
        if (space_is_visible(view->sid)) {
            window_node_flush(node);
        } else {
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    }
}

void window_manager_toggle_window_windowed_fullscreen(struct window *window)
{
    TIME_FUNCTION;

    uint32_t did = window_display_id(window->id);
    if (!did) return;

    if (window_check_flag(window, WINDOW_WINDOWED)) {
        window_clear_flag(window, WINDOW_WINDOWED);
        window_manager_animate_window((struct window_capture) { .window = window, .x = window->windowed_frame.origin.x , .y = window->windowed_frame.origin.y, .w = window->windowed_frame.size.width, .h = window->windowed_frame.size.height });
    } else {
        window_set_flag(window, WINDOW_WINDOWED);
        window->windowed_frame = window->frame;
        CGRect bounds = display_bounds_constrained(did, true);
        window_manager_animate_window((struct window_capture) { .window = window, .x = bounds.origin.x, .y = bounds.origin.y, .w = bounds.size.width, .h = bounds.size.height });
    }
}

void window_manager_toggle_window_expose(struct window *window)
{
    TIME_FUNCTION;

    window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    CoreDockSendNotification(CFSTR("com.apple.expose.front.awake"), 0);
}

static void window_manager_validate_windows_on_space(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count)
{
    int view_window_count;
    uint32_t *view_window_list = view_find_window_list(view, &view_window_count);

    for (int i = 0; i < view_window_count; ++i) {
        bool found = false;

        for (int j = 0; j < window_count; ++j) {
            if (view_window_list[i] == window_list[j]) {
                found = true;
                break;
            }
        }

        if (!found) {
            struct window *window = window_manager_find_window(wm, view_window_list[i]);
            if (!window) continue;

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            view_remove_window_node(view, window);
            window_manager_remove_managed_window(wm, window->id);

            view_set_flag(view, VIEW_IS_DIRTY);
        }
    }
}

static void window_manager_check_for_windows_on_space(struct window_manager *wm, struct view *view, uint32_t *window_list, int window_count)
{
    for (int i = 0; i < window_count; ++i) {
        struct window *window = window_manager_find_window(wm, window_list[i]);
        if (!window || !window_manager_should_manage_window(window)) continue;

        struct view *existing_view = window_manager_find_managed_window(wm, window);
        if (existing_view && existing_view->layout != VIEW_FLOAT && existing_view != view) {

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            view_remove_window_node(existing_view, window);
            window_manager_remove_managed_window(wm, window->id);
            view_set_flag(existing_view, VIEW_IS_DIRTY);
        }

        if (!existing_view || (existing_view->layout != VIEW_FLOAT && existing_view != view)) {

            //
            // @cleanup
            //
            // :AXBatching
            //
            // NOTE(asmvik): Batch all operations and mark the view as dirty so that we can perform a single flush,
            // making sure that each window is only moved and resized a single time, when the final layout has been computed.
            // This is necessary to make sure that we do not call the AX API for each modification to the tree.
            //

            view_add_window_node(view, window);
            window_manager_add_managed_window(wm, window, view);
            view_set_flag(view, VIEW_IS_DIRTY);
        }
    }
}

void window_manager_validate_and_check_for_windows_on_space(struct space_manager *sm, struct window_manager *wm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout == VIEW_FLOAT) return;

    int window_count = 0;
    uint32_t *window_list = space_window_list(sid, &window_count, false);
    window_manager_validate_windows_on_space(wm, view, window_list, window_count);
    window_manager_check_for_windows_on_space(wm, view, window_list, window_count);

    //
    // @cleanup
    //
    // :AXBatching
    //
    // NOTE(asmvik): Flush previously batched operations if the view is marked as dirty.
    // This is necessary to make sure that we do not call the AX API for each modification to the tree.
    //

    if (space_is_visible(view->sid) && view_is_dirty(view)) {
        window_node_flush(view->root);
        view_clear_flag(view, VIEW_IS_DIRTY);
    }
}

void window_manager_correct_for_mission_control_changes(struct space_manager *sm, struct window_manager *wm)
{
    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return;

    for (int i = 0; i < display_count; ++i) {
        uint32_t did = display_list[i];

        int space_count;
        uint64_t *space_list = display_space_list(did, &space_count);
        if (!space_list) continue;

        uint64_t sid = display_space_id(did);
        for (int j = 0; j < space_count; ++j) {
            if (space_list[j] == sid) {
                window_manager_validate_and_check_for_windows_on_space(sm, wm, sid);
            } else {
                space_manager_mark_view_invalid(sm, space_list[j]);
            }
        }
    }
}

void window_manager_handle_display_add_and_remove(struct space_manager *sm, struct window_manager *wm, uint32_t did)
{
    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list) return;

    for (int i = 0; i < space_count; ++i) {
        if (space_is_user(space_list[i])) {
            int window_count;
            uint32_t *window_list = space_window_list(space_list[i], &window_count, false);
            if (window_list) {
                struct view *view = space_manager_find_view(sm, space_list[i]);
                if (view->layout != VIEW_FLOAT) {
                    window_manager_check_for_windows_on_space(wm, view, window_list, window_count);
                }
            }
            break;
        }
    }

    uint64_t sid = display_space_id(did);
    for (int i = 0; i < space_count; ++i) {
        if (space_list[i] == sid) {
            space_manager_refresh_view(sm, sid);
        } else {
            space_manager_mark_view_invalid(sm, space_list[i]);
        }
    }
}

void window_manager_init(struct window_manager *wm)
{
    wm->system_element = AXUIElementCreateSystemWide();
    AXUIElementSetMessagingTimeout(wm->system_element, 1.0);

    wm->ffm_mode = FFM_DISABLED;
    wm->window_origin_mode = WINDOW_ORIGIN_DEFAULT;
    wm->enable_mff = false;
    wm->menubar_opacity = 1.0f;
    wm->insert_feedback_color = rgba_color_from_hex(0xffd75f5f);

    table_init(&wm->application, 150, hash_wm, compare_wm);
    table_init(&wm->window, 150, hash_wm, compare_wm);
    table_init(&wm->managed_window, 150, hash_wm, compare_wm);
    table_init(&wm->window_lost_focused_event, 150, hash_wm, compare_wm);
    table_init(&wm->application_lost_front_switched_event, 150, hash_wm, compare_wm);
    table_init(&wm->insert_feedback, 150, hash_wm, compare_wm);
}

void window_manager_begin(struct space_manager *sm, struct window_manager *wm)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    table_for (struct process *process, g_process_manager.process, {
        if (workspace_application_is_observable(process)) {
            struct application *application = application_create(process);

            if (application_observe(application)) {
                window_manager_add_application(wm, application);
                window_manager_add_existing_application_windows(sm, wm, application, -1);
            } else {
                application_unobserve(application);
                application_destroy(application);
            }
        } else {
            debug("%s: %s (%d) is not observable, subscribing to activationPolicy changes\n", __FUNCTION__, process->name, process->pid);
            workspace_application_observe_activation_policy(g_workspace_context, process);
        }
    })
    [pool drain];

    struct window *window = window_manager_focused_window(wm);
    if (window) {
        wm->last_window_id = window->id;
        wm->focused_window_id = window->id;
        wm->focused_window_psn = window->application->psn;
    }
}
