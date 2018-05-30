/* le bac: the badge audio composer
 * all bugs are qguv's fault */

#include "notes.h"
#include "help.h"

#include <stdio.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <termbox.h>
#include <unistd.h>
#include <time.h>

/* set only if your audio driver needs tweaking */
#define DRIVER NULL
#define ENCODING "s16"

/* badge audio rate */
#define RATE 38000

/* more mercy == less volume */
#define MERCY 4

#define TEMPO_MAX 255
#define TEMPO_MIN 16
#define TEMPO_JUMP 10

#define QUANTIZE(X, MINVAL, MAXVAL) (\
    ((X) == 0) ? 0 : \
    ((X) > 0) ? (MAXVAL) : \
    (MINVAL) \
)

struct line_t {
    char note;
    char lpnote; /* low-priority note */
};

enum column_t { NOTE, LPNOTE };
enum global_mode_t { SEQUENCER, HELP };
enum redraw { NORMAL, FULL };

struct line_t pattern[16];
int current_line = 0;

unsigned char tempo = 128;

void die(const char * const s)
{
    fputs(s, stderr);
    exit(1);
}

void tb_puts(const char *s, struct tb_cell *cell, int x, int y)
{
    char c;
    for (int i = 0; (c = s[i]); i++) {
        cell->ch = c;
        tb_put_cell(x + i, y, cell);
    }
}

void debug(const char *s)
{
    static int y = 3;

    struct tb_cell c;
    c.fg = TB_DEFAULT;
    c.bg = TB_DEFAULT;

    tb_puts(s, &c, 10, y++);
    if (y == 40)
        y = 3;
}

int audio_child(int *pid_p)
{
    /* set up pipes to communicate with audio child */
    int pipefds[2];
    int err = pipe(pipefds);
    if (err) {
        fputs("couldn't create a pipe\n", stderr);
        exit(1);
    }

    /* spin off a child to produce the audio */
    int pid = fork();
    if (pid == 0) {
        close(pipefds[1]);
        dup2(pipefds[0], STDIN_FILENO);
        close(pipefds[0]);
        execlp("out123", "out123", "--mono", "--encoding", "s16", "--rate", "38000", (char *) NULL);
    }
    if (pid_p != NULL)
        *pid_p = pid;

    close(pipefds[0]);
    return pipefds[1];
}

int samples_per_cycle(char note)
{
    double freq = note_freqs[note % 12];
    for (int i = 0; i < note / 12; i++)
        freq *= 2; /* wasting some cycles to get this right; errors accumulate */
    return (double) RATE / freq + 0.5L;
}

void audio(int audio_pipe)
{
    int samples_per_step = RATE * 15 / tempo;

    int cycle_samples, half_cycle_samples, quarter_cycle_samples, three_quarter_cycle_samples,
        lpcycle_samples, lphalf_cycle_samples, lpquarter_cycle_samples, lpthree_quarter_cycle_samples;

    for (int step = 0; step < 16; step++) {

        /* FIXME no-note cells should continue previous wave, not restart it */

        char note = pattern[step].note,
             lpnote = pattern[step].lpnote;

        if (note) {
            cycle_samples = samples_per_cycle(note);
            half_cycle_samples = cycle_samples >> 1;
            quarter_cycle_samples = half_cycle_samples >> 1; /* FIXME just 50% duty cycle for now */
            three_quarter_cycle_samples = half_cycle_samples + quarter_cycle_samples;
        }

        if (lpnote) {
            lpcycle_samples = samples_per_cycle(lpnote);
            lphalf_cycle_samples = lpcycle_samples >> 1;
            lpquarter_cycle_samples = lphalf_cycle_samples >> 1; /* FIXME just 50% duty cycle for now */
            lpthree_quarter_cycle_samples = lphalf_cycle_samples + lpquarter_cycle_samples;
        }

        for (int i = 0; i < samples_per_step; i++) {
            int pos = i % cycle_samples,
                lppos = i % lpcycle_samples;

            char note_level = (pos < quarter_cycle_samples) ? 1 :
                              (pos >= half_cycle_samples && pos < three_quarter_cycle_samples) ? -1 :
                              0;
            char lpnote_level = (lppos < lpquarter_cycle_samples) ? 1 :
                                (lppos >= lphalf_cycle_samples && lppos < lpthree_quarter_cycle_samples) ? -1 :
                                0;

            int16_t sample = QUANTIZE(note_level + lpnote_level, INT16_MIN >> MERCY, INT16_MAX >> MERCY);
            //DEBUG int16_t sample = SIGN(note_level + lpnote_level) ? (INT16_MAX >> MERCY) : (INT16_MIN >> MERCY);
            (void) note_level; // DEBUG
            write(audio_pipe, (char *) &sample, sizeof(sample));
        }
    }

    close(audio_pipe);
}

void deconstruct_note(char note, char * const note_name, char * const accidental, char * const octave)
{
    if (note == 0) {
        *note_name = *accidental = *octave = '-';
        return;
    }

    if (note > 63) {
        *note_name = *accidental = *octave = '!';
        return;
    }

    *note_name  = "bccddeffggaa"[note % 12];
    *accidental = "  # #  # # #"[note % 12];
    *octave = "234567"[(note - 1) / 12];
}

void draw_note_columns(enum column_t column)
{
    struct tb_cell bright;
    bright.fg = TB_DEFAULT;
    bright.bg = TB_CYAN;

    struct tb_cell dark;
    dark.fg = TB_DEFAULT;
    dark.bg = TB_BLACK;

    struct tb_cell black;
    black.fg = TB_DEFAULT;
    black.bg = TB_DEFAULT;

    struct tb_cell *cell;

    char note_name, accidental, octave, lpnote_name, lpaccidental, lpoctave;
    for (int row = 0; row < 16; row++) {

        deconstruct_note(pattern[row].note, &note_name, &accidental, &octave);
        deconstruct_note(pattern[row].lpnote, &lpnote_name, &lpaccidental, &lpoctave);

        cell = (row % 4 == 0) ? &bright : &dark;

        /* regular note column */
        cell->ch = note_name;
        tb_put_cell(3, row + 3, cell);
        cell->ch = accidental;
        tb_put_cell(4, row + 3, cell);
        cell->ch = octave;
        tb_put_cell(5, row + 3, cell);

        /* low-priority column */
        cell->ch = lpnote_name;
        tb_put_cell(7, row + 3, cell);
        cell->ch = lpaccidental;
        tb_put_cell(8, row + 3, cell);
        cell->ch = lpoctave;
        tb_put_cell(9, row + 3, cell);

        /* line number or left arrow */
        const char is_current_line = (row == current_line);
        const char left_arrow = is_current_line && column == NOTE;
        const char right_arrow = is_current_line && column == LPNOTE;

        black.ch = (left_arrow) ? '-' : "0123456789abcdef"[row];
        tb_put_cell(0, row + 3, &black);

        /* arrow blits over line number */
        black.ch = (left_arrow) ? '>' : ' ';
        tb_put_cell(1, row + 3, &black);

        /* right arrow */
        black.ch = (right_arrow) ? '<': ' ';
        tb_put_cell(11, row + 3, &black);
        black.ch = (right_arrow) ? '-': ' ';
        tb_put_cell(12, row + 3, &black);
    }
}

void draw_tempo(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    char s[4];
    snprintf(s, sizeof(s), "T:");
    tb_puts(s, &cell, 0, 1);

    cell.bg = TB_MAGENTA;
    snprintf(s, sizeof(s), "%3d", tempo);
    tb_puts(s, &cell, 3, 1);
}

void draw_help(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_MAGENTA;

    tb_puts("HELP", &cell, 1, 1);

    cell.bg = TB_DEFAULT;

    for (int i = 0; helptext[i][0]; i++) {
        cell.fg = TB_DEFAULT | TB_BOLD;
        tb_puts(helptext[i][0], &cell, 1, i + 3);

        cell.fg = TB_DEFAULT;
        tb_puts(helptext[i][1], &cell, 8, i + 3);
    }
}

void draw_not_quit(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT;
    cell.bg = TB_DEFAULT;

    tb_puts("the quit key is ctrl-c  ", &cell, 17, 1);
}

int main(void)
{
    int err = tb_init();
    if (err) {
        die("Termbox failed to initialize\n");
    }

    struct timespec tempo_input, last_tempo_input;
    last_tempo_input.tv_sec = 0;

    struct tb_event event;

    char last_edit = 25;

    enum redraw redraw_setting = FULL;
    enum global_mode_t global_mode = SEQUENCER;

    struct tb_cell cell;
    cell.fg = TB_DEFAULT;
    cell.bg = TB_DEFAULT;

    char quit_request = 0;

    enum column_t column = NOTE;

    char *edit_note;

    for (;;) {

        if (redraw_setting == FULL) {
            tb_clear();
            if (global_mode == SEQUENCER)
                draw_tempo();
            else if (global_mode == HELP)
                draw_help();
            tb_puts("le bac / the badge audio composer", &cell, 8, 1);
        }

        if (global_mode == SEQUENCER) {
            draw_note_columns(column);
            edit_note = (column == NOTE) ? &pattern[current_line].note : &pattern[current_line].lpnote;
        }

        tb_present();

        redraw_setting = NORMAL;

        err = tb_poll_event(&event);
        if (err < 0)
            tb_puts("termbox event error :(  ", &cell, 17, 1);

        if (event.type == TB_EVENT_RESIZE) {
            redraw_setting = FULL;
            continue;
        }

        if (event.type == TB_EVENT_MOUSE)
            continue;

        /* help mode handles keys differently */
        if (global_mode == HELP) {
            global_mode = SEQUENCER;
            redraw_setting = FULL;
            continue;
        }

        if (event.key == TB_KEY_CTRL_C) {

            /* quit request confirmed? */
            if (quit_request) {
                tb_shutdown();
                return 0;
            }

            /* quit request issued? */
            tb_puts("press again to quit     ", &cell, 17, 1);
            quit_request = 1;
            continue;

        /* quit request cancelled? */
        } else if (quit_request) {
            redraw_setting = FULL;
            quit_request = 0;
        }

        /* special key pressed */
        if (!event.ch) {
            switch (event.key) {

            case TB_KEY_ESC:
                draw_not_quit();
                break;
            case TB_KEY_ARROW_LEFT:
                event.ch = 'h';
                break;
            case TB_KEY_ARROW_DOWN:
                event.ch = 'j';
                break;
            case TB_KEY_ARROW_UP:
                event.ch = 'k';
                break;
            case TB_KEY_ARROW_RIGHT:
                event.ch = 'l';
                break;

            case TB_KEY_ENTER:
                if (!fork()) {
                    /* TODO: send SIGTERM when parent dies */
                    int audio_pipe = audio_child(NULL);
                    audio(audio_pipe);
                    exit(0);
                }
                break;

            case TB_KEY_TAB:
                column = (column == NOTE) ? LPNOTE : NOTE;
                break;

            case TB_KEY_BACKSPACE2:
            case TB_KEY_DELETE:
                *edit_note = 0;
                break;
            }
        }

        switch (event.ch) {

        case 'Q':
        case 'q':
            draw_not_quit();
            break;

        case 'C':
            for (int i = 0; i < 16; i++)
                pattern[i].note = 0;
            break;

        /* tap tempo */
        case 'T':
            clock_gettime(CLOCK_REALTIME, &tempo_input);

            /* calculate new tempo */
            if (last_tempo_input.tv_sec != 0) {
                if (tempo_input.tv_sec - last_tempo_input.tv_sec < 3) {
                    double x = (double) (tempo_input.tv_sec) - (double) (last_tempo_input.tv_sec);
                    x += (tempo_input.tv_nsec - last_tempo_input.tv_nsec) * 1e-9;
                    x = 60.0L / x + 0.5L;
                    if (x > TEMPO_MIN && x < TEMPO_MAX) {
                        tempo = x;
                        draw_tempo();
                    }
                }
            }

            last_tempo_input.tv_sec = tempo_input.tv_sec;
            last_tempo_input.tv_nsec = tempo_input.tv_nsec;
            break;

        case 'H':
            if (*edit_note == 0)
                *edit_note = last_edit;

            if (*edit_note < 13)
                last_edit = *edit_note = 1;
            else
                last_edit = *edit_note -= 12;
            break;

        case 'h':
            if (*edit_note == 0)
                *edit_note = last_edit;

            if (*edit_note > 1)
                last_edit = --*edit_note;
            break;

        case 'j':
            current_line++;
            current_line %= 16;
            break;

        case 'k':
            current_line += 15;
            current_line %= 16;
            break;

        case 'l':
            if (*edit_note == 0)
                *edit_note = last_edit;

            if (*edit_note < 63)
                last_edit = ++*edit_note;
            break;

        case 'L':
            if (*edit_note == 0)
                *edit_note = last_edit;

            if (*edit_note > 51)
                last_edit = *edit_note = 63;
            else
                last_edit = *edit_note += 12;
            break;

        case '=':
            if (tempo < TEMPO_MAX)
                tempo++;
            draw_tempo();
            break;

        case '-':
            if (tempo > TEMPO_MIN)
                tempo--;
            draw_tempo();
            break;

        case '+':
            if (tempo <= TEMPO_MAX - TEMPO_JUMP)
                tempo += TEMPO_JUMP;
            else
                tempo = TEMPO_MAX;
            draw_tempo();
            break;

        case '_':
            if (tempo >= TEMPO_MIN + TEMPO_JUMP)
                tempo -= TEMPO_JUMP;
            else
                tempo = TEMPO_MIN;
            draw_tempo();
            break;

        case '.':
            *edit_note = last_edit;
            break;

        case '?':
            global_mode = HELP;
            redraw_setting = FULL;
            quit_request = 0;
            break;
        }
    }
}
