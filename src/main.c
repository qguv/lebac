/* le bac: the badge audio composer
 * all bugs are qguv's fault */

#include "notes.h"
#include "help.h"

#include <termbox.h>

#include <fcntl.h> // open()
#include <stdint.h> // int16_t
#include <stdio.h> // snprintf, fputs (errors)
#include <stdlib.h> // exit
#include <time.h> // clock_gettime
#include <unistd.h> // pipe, fork, close, dup, exec, ...

/* set only if your audio driver needs tweaking */
#define DRIVER NULL
#define ENCODING "s16"

/* crunch into 1.5 bit space? */
#define EMULATE_SHITTY_BADGE_AUDIO 1

/* badge audio rate */
#define RATE 38000

/* more mercy == less volume */
#define MERCY 2

#define TEMPO_MAX 255
#define TEMPO_MIN 16
#define TEMPO_JUMP 10

#define NOTE_MAX 97

#define QUANTIZE(X, MINVAL, MAXVAL) (\
    ((X) == 0) ? 0 : \
    ((X) > 0) ? (MAXVAL) : \
    (MINVAL) \
)

const char * const duties[] = {
    "!!",
    "50",
    "25",
    "12",
    " 6",
    " 3",
    " 1",
    "L1",
    "L2",
    "L3",
    "L4",
    "L5",
    "L6",
    "L7",
    "L8",
};

struct note_t {
    char note;
    char duty;
};

enum column_t { LEFT, RIGHT };
enum global_mode_t { SEQUENCER, HELP };
enum redraw { NORMAL, FULL };

struct page_t {
    struct note_t notes[16][2];
    struct page_t *next, *prev;
};

struct page_t *page, *tmp_page;
int page_num = 1;
int num_pages = 1;

int current_line = 0;

unsigned char tempo = 128;

/* default colors */
struct tb_cell dcell;

void die(const char * const s)
{
    fputs(s, stderr);
    exit(1);
}

void tb_puts(const char * const s, struct tb_cell *cell, int x, int y)
{
    char c;
    for (int i = 0; (c = s[i]); i++) {
        cell->ch = c;
        tb_put_cell(x + i, y, cell);
    }
}

void debug(const char * const s)
{
    static int y = 3;

    tb_puts(s, &dcell, 10, y++);
    if (y == 40)
        y = 3;
}

int audio_child(int * const pid_p)
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
    if (note <= 0)
        return 0;

    double freq = note_freqs[note % 12];
    for (int i = 0; i < note / 12; i++)
        freq *= 2; /* wasting some cycles to get this right; errors accumulate */
    return (double) RATE / freq + 0.5L;
}

/* call audio with the output of audio_child to play audio. you need a new
 * audio_child pipe each time. we will close the audio pipe for you when it's
 * done playing.
 *
 * the synthesizer alg creates triangle wave approximations with varying duty
 * cycles. the waves look like this:
 *
 *
 * 50% duty:
 *
 *      begin_high
 *     /        end_high
 *    /        /          begin_low
 *   ,________,          /               ,________,
 *   |        |         /                |        |
 * __|        |________,        ,________|        |________,        ,___...
 *                     |        |                          |        |
 *                     |________|                          |________|
 *
 *                               \
 *                                end_low
 *
 *   |-----------------------------------|
 *                one wavelength
 *                cycle_samples
 *
 * -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -
 *
 *  12% duty:
 *
 *   ,___,                               ,___,
 *   |   |                               |   |
 * __|   |_____________,   ,_____________|   |_____________,   ,________...
 *                     |   |                               |   |
 *                     |___|                               |___|
 *
 *
 *   |-----------------------------------|
 *                one wavelength
 *
 * -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -
 *
 * there are two such waves which are added to produce a result--this is what
 * is meant by "two-channel audio".
 *
 * if EMULATE_SHITTY_BADGE_AUDIO is enabled, we'll then restrict the wave to
 * three possible values: INT16_MAX, 0, and INT16_MIN, representing the push/1,
 * neutralize/0, and pull/-1 states the H-bridge can take when driving the
 * buzzer on the badge. this gives you a very accurate simulation of how your
 * composition will sound when playing on the badge.
 */
void audio(int audio_pipe)
{
    /* duration of one sequencer step, in samples */
    const int samples_per_step = RATE * 15 / tempo;

    /* position of each wave in its oscillation */
    int cycle_pos[2] = {0, 3};

    /* whether the wave is currently playing */
    char play[2] = {0, 0};

    /* scroll back to the first page */
    struct page_t *playing_page = page;
    while (playing_page->prev != NULL)
        playing_page = playing_page->prev;

    while (playing_page) {
        for (int step = 0; step < 16; step++) {

            /* wavelength, in samples */
            int cycle_samples[2];

            /* important points of the wavelength, in number of samples since the
             * start of the wave */
            int end_high[2], begin_low[2], end_low[2];

            for (int channel = 0; channel < 2; channel++) {

                char note = playing_page->notes[step][channel].note;
                char duty = playing_page->notes[step][channel].duty;

                if (note > 0) {
                    play[channel] = 1;
                    cycle_samples[channel] = samples_per_cycle(note);

                    /* make math cheaper per sample by doing it upfront */
                    begin_low[channel] = cycle_samples[channel] >> 1;
                    end_high[channel] = begin_low[channel] >> duty;
                    end_low[channel] = begin_low[channel] + end_high[channel];

                } else if (note < 0) {
                    play[channel] = 0;
                }
                /* else note == 0, so continue oscillating previous note */
            }

            for (int i = 0; i < samples_per_step; i++) {
                int16_t sample = 0;
                for (int channel = 0; channel < 2; channel++) {

                    if (!play[channel])
                        continue;

                    int pos = cycle_pos[channel];

                    /* first half of the wave */
                    if (pos < begin_low[channel]) {

                        /* within the high section */
                        if (pos < end_high[channel]) {
                            sample += INT16_MAX >> (1 + MERCY);
                        }

                    /* second half of the wave */
                    } else {

                        /* within the low section */
                        if (pos < end_low[channel]) {
                            sample += INT16_MIN >> (1 + MERCY);
                        }
                    }

                    cycle_pos[channel]++;
                    if (cycle_pos[channel] >= cycle_samples[channel]) {
                        cycle_pos[channel] = 0;
                    }
                }

                /* at this point, we're in two-and-a-half bit space: [-2, -1, 0, 1, 2 ]
                 *
                 *            lpnote_level
                 *           | -1   0   1
                 *         --+------------
                 * note_  -1 | -2  -1   0
                 * level   0 | -1   0   1
                 *         1 |  0   1   2
                 *
                 * where 2 and 1 represent INT16_MAX and INT16_MAX/2, respectively
                 *
                 * the H-bridge + buzzer combo can only drive the speaker forward
                 * (1), backward (-1), or toward a rest state (0). if requested, we
                 * can emulate how that would sound by slamming all positive
                 * numbers up to INT16_MAX and all negative numbers down to
                 * INT16_MIN.
                 */
                if (EMULATE_SHITTY_BADGE_AUDIO)
                    sample = QUANTIZE(sample, INT16_MIN >> MERCY, INT16_MAX >> MERCY);

                write(audio_pipe, (char *) &sample, sizeof(sample));
            }
        }
        playing_page = playing_page->next;
    }

    close(audio_pipe);
}

void tb_put_note(const struct note_t * const note, struct tb_cell * const cell, int x, int y)
{
    char note_name, accidental, octave;

    if (note->note == 0) {
        note_name = accidental = octave = '-';
    } else if (note->note > NOTE_MAX) {
        note_name = accidental = octave = '!';
    } else if (note->note < 0) {
        note_name = octave = ' ';
        accidental = 'K';
    } else {
        note_name  = "bccddeffggaa"[note->note % 12];
        accidental = "  # #  # # #"[note->note % 12];
        octave = "23456789"[(note->note - 1) / 12];
    }

    cell->ch = note_name;
    tb_put_cell(x, y, cell);

    cell->ch = accidental;
    tb_put_cell(x + 1, y, cell);

    cell->ch = octave;
    tb_put_cell(x + 2, y, cell);

    if (note->note > 0) {
        tb_puts(duties[(int) note->duty], cell, x + 4, y);
        cell->ch = '%';
        tb_put_cell(x + 6, y, cell);
    } else {
        tb_puts("---", cell, x + 4, y);
    }
}

void draw_note_columns(enum column_t selected_column)
{
    struct tb_cell bright;
    bright.fg = TB_DEFAULT;
    bright.bg = TB_CYAN;

    struct tb_cell dark;
    dark.fg = TB_DEFAULT;
    dark.bg = TB_BLACK;

    struct tb_cell *cell;

    for (int row = 0; row < 16; row++) {

        cell = (row % 4 == 0) ? &bright : &dark;

        /* note columns */
        for (int i = 0; i < 2; i++)
            tb_put_note(&page->notes[row][i], cell, 9 * i + 3, row + 4);

        /* line number or left arrow */
        const char is_current_line = (row == current_line);
        const char left_arrow = is_current_line && selected_column == LEFT;
        const char right_arrow = is_current_line && selected_column == RIGHT;

        dcell.ch = (left_arrow) ? '-' : "0123456789abcdef"[row];
        tb_put_cell(0, row + 4, &dcell);

        /* arrow blits over line number */
        dcell.ch = (left_arrow) ? '>' : ' ';
        tb_put_cell(1, row + 4, &dcell);

        /* right arrow */
        dcell.ch = (right_arrow) ? '<': ' ';
        tb_put_cell(20, row + 4, &dcell);
        dcell.ch = (right_arrow) ? '-': ' ';
        tb_put_cell(21, row + 4, &dcell);
    }
}

void draw_tempo(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    char s[4];
    tb_puts("TPO", &cell, 0, 1);

    cell.bg = TB_MAGENTA;
    snprintf(s, sizeof(s), "%3d", tempo);
    tb_puts(s, &cell, 4, 1);
}

void draw_page_num(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    char s[8];
    cell.bg = TB_MAGENTA;
    snprintf(s, sizeof(s), "%02x / %02x", page_num, num_pages);
    tb_puts(s, &cell, 0, 2);
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
    tb_puts("the quit key is ctrl-c  ", &dcell, 17, 1);
}

void save(char *songfile)
{
#if PIGSFLY
    /* FIXME detect malformatted files */
    /* TODO allow filenames to be specified at load */

    int fd = open(songfile, O_CREAT | O_RDWR);
    if (fd < 0) {
        tb_puts("can't open file         ", &dcell, 17, 1);
        tb_puts(songfile, &dcell, 33, 1);
        return;
    }

    /* rewind to the first page */
    struct page_t *saving_page = page;
    while (saving_page->prev != NULL)
        saving_page = saving_page->prev;

    while (saving_page) {

        int to_write = sizeof(struct page_t);
        while (written < to_write) {
            written += write(fd, ((char *) page->notes) + written, to_write - written);
        }

        saving_page = saving_page->next;
    }

    close(fd);
#else
    (void) songfile;
#endif /* PIGSFLY */
}

void load(char *songfile)
{
#if PIGSFLY
    /* FIXME detect malformatted files */
    /* TODO allow filenames to be specified at load */
    int fd = open(songfile, O_RDONLY);
    if (fd < 0) {
        tb_puts("no permissions for      ", &dcell, 17, 1);
        tb_puts(songfile, &dcell, 36, 1);
        return;
    }

    /* rewind to the first page */
    struct page_t *tmp, *saving_page = page;
    while (saving_page->prev != NULL)
        saving_page = saving_page->prev;

    while (saving_page) {
        tmp = saving_page->next;
        free(saving_page);
        saving_page = tmp;
    }

    num_pages = 0;
    page = 1;

    int readed
    while (i < (int) sizeof(page->notes))
        i += read(fd, &page->notes[i], sizeof(page->notes) - i);
    close(fd);
#else
    (void) songfile;
#endif /* PIGSFLY */
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        die("Usage: lebac [file]\n");
    char *songfile = argv[1];

    int err = tb_init();
    if (err) {
        die("Termbox failed to initialize\n");
    }

    page = calloc(1, sizeof(struct page_t));

    struct timespec tempo_input, last_tempo_input;
    last_tempo_input.tv_sec = 0;

    struct tb_event event;

    struct note_t last_edit;
    last_edit.note = 25;
    last_edit.duty = 1;

    enum redraw redraw_setting = FULL;
    enum global_mode_t global_mode = SEQUENCER;

    dcell.fg = TB_DEFAULT;
    dcell.bg = TB_DEFAULT;

    char quit_request = 0;

    enum column_t column = LEFT;

    struct note_t *edit_note;

    for (;;) {

        if (redraw_setting == FULL) {
            tb_clear();
            if (global_mode == SEQUENCER) {
                draw_tempo();
                draw_page_num();
            } else if (global_mode == HELP) {
                draw_help();
            }
            tb_puts("le bac / the badge audio composer", &dcell, 8, 1);
        }

        if (global_mode == SEQUENCER) {
            draw_note_columns(column);
            edit_note = (column == LEFT) ? &page->notes[current_line][0] : &page->notes[current_line][1];
        }

        tb_present();

        redraw_setting = NORMAL;

        err = tb_poll_event(&event);
        if (err < 0)
            tb_puts("termbox event error :(  ", &dcell, 17, 1);

        if (event.type == TB_EVENT_RESIZE) {
            redraw_setting = FULL;
            continue;
        }

        if (event.type == TB_EVENT_MOUSE)
            continue;

        /* help mode handles keys separately */
        if (global_mode == HELP) {
            quit_request = 0;
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
            tb_puts("press again to quit     ", &dcell, 17, 1);
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

            /* NOT quit--it's too easy to press. plus it's triggered when you
             * do crazy shit like Shift-Arrow */
            case TB_KEY_ESC:
                draw_not_quit();
                break;

            /* keyboard key aliases for the uninitiated */
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

            /* play audio */
            case TB_KEY_ENTER:
                if (!fork()) {
                    /* TODO: send SIGTERM when parent dies */
                    int audio_pipe = audio_child(NULL);
                    audio(audio_pipe);
                    exit(0);
                }
                break;

            /* switch columns */
            case TB_KEY_TAB:
                column = (column == LEFT) ? RIGHT : LEFT;
                break;

            /* delete this note */
            case TB_KEY_BACKSPACE2:
            case TB_KEY_DELETE:
                if (edit_note->note > 0) {
                    last_edit.note = edit_note->note;
                    last_edit.duty = edit_note->duty;
                }
                edit_note->note = 0;
                break;
            }
        }

        switch (event.ch) {

        /* NOT quit--it's too easy to press */
        case 'Q':
        case 'q':
            draw_not_quit();
            break;

        /* clear this page */
        case 'C':
            for (int i = 0; i < 16; i++) {
                for (int channel = 0; channel < 2; channel++) {
                    page->notes[i][channel].note = 0;
                    page->notes[i][channel].duty = 0;
                }
            }
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

        /* jump down an octave */
        case 'H':
            if (edit_note->note <= 0) {
                edit_note->note = last_edit.note;
                edit_note->duty = last_edit.duty;
            }

            if (edit_note->note < 13)
                last_edit.note = edit_note->note = 1;
            else
                last_edit.note = edit_note->note -= 12;
            last_edit.duty = edit_note->duty;

            break;

        /* decrease note */
        case 'h':
            if (edit_note->note <= 0) {
                edit_note->note = last_edit.note;
                edit_note->duty = last_edit.duty;
            }

            if (edit_note->note > 1) {
                last_edit.note = --edit_note->note;
                last_edit.duty = edit_note->duty;
            }
            break;

        /* move up a line */
        case 'j':
            if (current_line == 15) {
                if (page->next == NULL) {
                    page->next = calloc(1, sizeof(struct page_t));
                    page->next->prev = page;
                    num_pages++;
                }
                page = page->next;
                page_num++;
                redraw_setting = FULL;
                current_line = 0;
            } else {
                current_line++;
            }
            break;

        /* move down a line */
        case 'k':
            if (current_line == 0) {
                if (page_num > 1) {
                    page = page->prev;
                    page_num--;
                    redraw_setting = FULL;
                    current_line = 15;
                }
            } else {
                current_line--;
            }
            break;

        /* next page */
        case 'J':
            if (page->next == NULL) {
                page->next = calloc(1, sizeof(struct page_t));
                page->next->prev = page;
                num_pages++;
            }
            page = page->next;
            page_num++;
            redraw_setting = FULL;
            break;

        /* prev page */
        case 'K':
            if (page_num > 1) {
                page = page->prev;
                page_num--;
                redraw_setting = FULL;
                current_line = 15;
            }
            break;

        /* increase note */
        case 'l':
            if (edit_note->note <= 0) {
                edit_note->note = last_edit.note;
                edit_note->duty = last_edit.duty;
            }

            if (edit_note->note < 63) {
                last_edit.note = ++edit_note->note;
                last_edit.duty = edit_note->duty;
            }
            break;

        /* jump up an octave */
        case 'L':
            if (edit_note->note <= 0) {
                edit_note->note = last_edit.note;
                edit_note->duty = last_edit.duty;
            }

            if (edit_note->note > 51)
                last_edit.note = edit_note->note = 63;
            else
                last_edit.note = edit_note->note += 12;
            edit_note->duty = last_edit.duty;
            break;

        /* kill a sustained note */
        case 'x':
            edit_note->note = -1;
            break;

        /* delete current page */
        case 'X':
            if (num_pages > 1) {

                /* introduce the neighbors to each other */
                if (page->next)
                    page->next->prev = page->prev;
                if (page->prev)
                    page->prev->next = page->next;

                /* and get your records in order */
                num_pages--;
                redraw_setting = FULL;

                /* now you can die in peace */
                if ((tmp_page = page->prev)) {
                    tmp_page = page->prev;
                    free(page);
                    page = tmp_page;
                    page_num--;

                /* unless your next of kin is dead? ugh i'm reaching here */
                } else {
                    tmp_page = page->next;
                    free(page);
                    page = tmp_page;
                }
            }
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
            edit_note->note = last_edit.note;
            edit_note->duty = last_edit.duty;
            break;

        case '?':
            global_mode = HELP;
            redraw_setting = FULL;
            break;

        case 'S':
            save(songfile);
            tb_puts("saved to                ", &dcell, 17, 1);
            tb_puts(songfile, &dcell, 26, 1);
            break;

        case 'D':
            load(songfile);
            tb_puts("loaded                  ", &dcell, 17, 1);
            tb_puts(songfile, &dcell, 24, 1);
            break;
        }
    }
}
