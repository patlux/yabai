#ifndef SA_H
#define SA_H

extern unsigned char __src_osax_payload[];
extern unsigned int __src_osax_payload_len;
extern unsigned char __src_osax_loader[];
extern unsigned int __src_osax_loader_len;

int scripting_addition_load(void);
int scripting_addition_uninstall(void);

bool scripting_addition_create_space(uint64_t sid);
bool scripting_addition_destroy_space(uint64_t sid);
bool scripting_addition_focus_space(uint64_t sid);
bool scripting_addition_move_space_to_display(uint64_t src_sid, uint64_t dst_sid, uint64_t src_prev_sid, bool focus);
bool scripting_addition_move_space_after_space(uint64_t src_sid, uint64_t dst_sid, bool focus);
bool scripting_addition_move_window(uint32_t wid, int x, int y);
bool scripting_addition_focus_window(uint32_t wid);
bool scripting_addition_move_window_list_to_space(uint64_t sid, uint32_t *window_list, int window_count);
bool scripting_addition_move_window_to_space(uint64_t sid, uint32_t wid);

#endif
