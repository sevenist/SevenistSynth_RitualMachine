// The Ritual Machine's card layout, handed to the storage HAL (core/card_layout.h).
#include "core/card_layout.h"

// The card's folders, and what an older layout had elsewhere: its samples folder and keys.cfg at the root move into system/ when
// the user agrees to the folders (the ASK popup, storage_make_folders).
void card_layout_apply(void) {
    static const char *const folders[STORAGE_FOLDER_COUNT] = STORAGE_FOLDERS;
    static const storage_move_t moves[] = {{"samples", STORAGE_DIR_SAMPLES}, {"keys.cfg", STORAGE_DIR_CONFIG "/keys.cfg"}};
    storage_set_folders(folders, STORAGE_FOLDER_COUNT);
    storage_set_moves(moves, (int)(sizeof moves / sizeof moves[0]));
}
