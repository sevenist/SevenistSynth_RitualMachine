#pragma once
// Popup messages: a small box drawn in the middle of the display, over whatever screen is up.
//
//   INFO   a short note that goes away by itself (the value a Shift knob sets, "SD card inserted"). Not modal: input still goes to the
//          screen behind it. A new INFO replaces the one on screen.
//   ERROR  a message under an "ERROR" title (or the given one) with an OK button. Modal: stays until confirmed.
//   ASK    a question with Yes / No buttons; the answer goes to a callback. Modal.
//   CHOOSE a list of options under a title, one focused; the chosen one's index goes to a callback (-1: closed with Back). Modal. A
//          list longer than the screen scrolls around the focus. Its options are the caller's strings, or are asked from a label
//          callback as they are drawn (no copy: a list of 69 names costs nothing); a label may be a heading, which the focus skips.
//
// Modal popups wait in a short queue and are shown one at a time, oldest first; one on screen hides the INFO. While a modal popup is up the
// app hands it the ACTIONS of the binding table and of the key layout (not the raw controls), so the user's key layout works as everywhere
// else: Nav left / right (a list: up / down too), Value and Row change the focused button, Latch / Select press it, Back answers No
// (closes an error, closes a list without a choice).
// Notes, Shift and the master volume keep working; everything else is ignored, so nothing edits the screen behind the popup.
//
//   popup_ask(&app->popup, "SD CARD", "The card is not formatted. Format it?", false, on_format, app);
//   ...on_format(void *ctx, bool yes) runs once, after the popup closed (it may raise another popup).
#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"
#include "ui/gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { POPUP_NONE, POPUP_INFO, POPUP_ERROR, POPUP_ASK, POPUP_CHOOSE } popup_kind_t;

typedef void (*popup_answer_fn)(void *ctx, bool yes);
typedef void (*popup_choice_fn)(void *ctx, int choice);       // the option's index, -1 = closed with Back
typedef bool (*popup_label_fn)(void *ctx, int i, char *out, int n);   // option i's text into out; true = a heading (not focusable)

#define POPUP_TITLE_LEN 20
#define POPUP_TEXT_LEN  72
#define POPUP_QUEUE     4          // modal popups waiting (a full queue drops the new one)
#define POPUP_INFO_MS   1200       // default lifetime of an INFO
#define POPUP_CHOICES   128        // options of a CHOOSE at most
#define POPUP_LABEL_LEN 24         // a label's text at most (popup_label_fn)

typedef struct {
    uint8_t         kind;          // popup_kind_t
    char            title[POPUP_TITLE_LEN];
    char            text[POPUP_TEXT_LEN];   // word-wrapped to the box; '\n' starts a new line
    int8_t          arrow;         // INFO: 0 = none, -1 / +1 = an arrow left / right after the text (the way to turn a knob)
    bool            yes;           // ASK: the focused button
    popup_answer_fn answer;        // ASK / ERROR: called once when it closes (NULL: none); an ERROR always answers yes
    void           *ctx;
    const char *const *options;    // CHOOSE: the caller's strings (they must live until it closes), or NULL ...
    popup_label_fn  label;         // ... and the labels asked as they are drawn
    uint8_t         n_options;     // ... how many
    uint8_t         focus;         // ... the focused one
    popup_choice_fn choose;        // ... called once when it closes
} popup_msg_t;

typedef struct {
    popup_msg_t info;              // the INFO (kind POPUP_NONE when there is none)
    uint32_t    info_until;        // ... and when it goes
    popup_msg_t queue[POPUP_QUEUE];// modal popups, queue[0] is the one on screen
    int         count;
} popup_t;

void popup_init(popup_t *p);

// Raising. `title` may be NULL (ERROR: "ERROR"; INFO / ASK: no title bar). Returns false when the modal queue is full.
void popup_info(popup_t *p, const char *title, const char *text, int arrow, uint32_t now_ms, uint32_t ms);
bool popup_error(popup_t *p, const char *title, const char *text, popup_answer_fn answer, void *ctx);
bool popup_ask(popup_t *p, const char *title, const char *text, bool default_yes, popup_answer_fn answer, void *ctx);
// A list of `count` options (at most POPUP_CHOICES, each a short line), `focus` focused. The strings are not copied.
bool popup_choose(popup_t *p, const char *title, const char *const *options, int count, int focus, popup_choice_fn choose, void *ctx);
// The same with the labels asked from `label(ctx, i, ...)` while it is up; a heading is never focused (`focus` on one: the next
// option). The callback's index counts the headings too.
bool popup_choose_labels(popup_t *p, const char *title, int count, int focus, popup_label_fn label, popup_choice_fn choose, void *ctx);

bool popup_modal(const popup_t *p);                // a modal popup is up: give it the input
bool popup_tick(popup_t *p, uint32_t now_ms);      // true when an INFO just went (redraw)
bool popup_info_showing(const popup_t *p);         // an INFO is up (it may be hidden behind a modal one)

// Input for the modal popup on screen.
void popup_move(popup_t *p, int dir);              // dir < 0: towards Yes (left) / the option above, > 0: towards No (right) / below
void popup_confirm(popup_t *p);                    // press the focused button
void popup_cancel(popup_t *p);                     // Back: No / close the error

// Draws the popup on screen (the modal one, else the INFO) over what is in the buffer. Does not send the buffer.
void popup_draw(const popup_t *p, u8g2_t *g, const gui_style_t *st);

#ifdef __cplusplus
}
#endif
