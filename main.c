/* le bac: the badge audio composer
 * all bugs are qguv's fault */

#include "notes.h"

#include <stdio.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <termbox.h>
#include <unistd.h>

/* set only if your audio driver needs tweaking */
#define DRIVER NULL
#define ENCODING "s16"

/* badge audio rate */
#define RATE 38000

/* more mercy == less volume */
#define MERCY 4

struct line_t {
    char note;
};

struct line_t pattern[16];

int current_line = 0;
int tempo = 128;

int pipefd[2];

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

int audio_child(void)
{
    /* set up pipes to communicate with audio child */
    int pipefd[2];
    int err = pipe(pipefd);
    if (err) {
        fputs("couldn't create a pipe\n", stderr);
        exit(1);
    }

    /* spin off a child to produce the audio */
    int pid = fork();
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execlp("out123", "out123", "--mono", "--encoding", "s16", "--rate", "38000", (char *) NULL);
    }

    close(pipefd[0]);
    return pipefd[1];
}

void audio(void)
{
    int audio_pipe = audio_child();

    int samples_per_step = RATE * 15 / tempo;

    int cycle_samples, half_cycle_samples, quarter_cycle_samples, three_quarter_cycle_samples;
    for (int step = 0; step < 16; step++) {

        /* FIXME no-note cells should continue previous wave, not restart it */

        char note = pattern[step].note;
        if (note) {
            double freq = note_freqs[note % 12];
            for (int i = 0; i < note / 12; i++)
                freq *= 2; /* wasting some cycles to get this right; errors accumulate */
            cycle_samples = (double) RATE / freq + 0.5L;
            half_cycle_samples = cycle_samples >> 1;
            quarter_cycle_samples = half_cycle_samples >> 1; /* FIXME just 50% duty cycle for now */
            three_quarter_cycle_samples = half_cycle_samples + quarter_cycle_samples;
        }

        for (int i = 0; i < samples_per_step; i++) {
            int pos = i % cycle_samples;
            int16_t sample;
            if (pos < quarter_cycle_samples) {
                sample = INT16_MAX >> MERCY;
            } else if (pos >= half_cycle_samples && pos < three_quarter_cycle_samples) {
                sample = INT16_MIN >> MERCY;
            } else {
                sample = 0;
            }
            write(audio_pipe, (char *) &sample, sizeof(sample));
        }
    }

    close(audio_pipe);
}

void deconstruct_note(const struct line_t * const line, char * const note_name, char * const accidental, char * const octave)
{
    if (line->note == 0) {
        *note_name = *accidental = *octave = '-';
        return;
    }

    if (line->note > 63) {
        *note_name = *accidental = *octave = '!';
        return;
    }

    *note_name  = "bccddeffggaa"[line->note % 12];
    *accidental = "  # #  # # #"[line->note % 12];
    *octave = "234567"[line->note / 12];
}

void draw_note_column(void)
{
    struct tb_cell bright;
    bright.fg = TB_BLACK;
    bright.bg = TB_CYAN;

    struct tb_cell dark;
    dark.fg = TB_DEFAULT;
    dark.bg = TB_BLACK;

    struct tb_cell black;
    black.fg = TB_DEFAULT;
    black.bg = TB_DEFAULT;

    char note_name, accidental, octave;
    for (int row = 0; row < 16; row++) {

        deconstruct_note(&pattern[row], &note_name, &accidental, &octave);

        if (row % 4 == 0) {
            bright.ch = note_name;
            tb_put_cell(3, row + 3, &bright);
            bright.ch = accidental;
            tb_put_cell(4, row + 3, &bright);
            bright.ch = octave;
            tb_put_cell(5, row + 3, &bright);
        } else {
            dark.ch = note_name;
            tb_put_cell(3, row + 3, &dark);
            dark.ch = accidental;
            tb_put_cell(4, row + 3, &dark);
            dark.ch = octave;
            tb_put_cell(5, row + 3, &dark);
        }

        black.ch = (row == current_line) ? '-' : "0123456789abcdef"[row];
        tb_put_cell(0, row + 3, &black);
        black.ch = (row == current_line) ? '>' : ' ';
        tb_put_cell(1, row + 3, &black);
    }
}

void draw_tempo(void)
{
    struct tb_cell cell;
    cell.fg = TB_BLACK | TB_BOLD;
    cell.bg = TB_MAGENTA;

    char s[4];
    snprintf(s, sizeof(s), "%3d", tempo);
    tb_puts(s, &cell, 1, 1);
}

int main(void)
{
    int err = tb_init();
    if (err) {
        die("Termbox failed to initialize\n");
    }

    struct tb_event event;

    char last_edit = 25;

    struct tb_cell cell;
    cell.fg = TB_DEFAULT;
    cell.bg = TB_DEFAULT;

full_redraw:

    tb_clear();
    draw_tempo();
    tb_puts("le bac: the badge audio composer", &cell, 5, 1);
    draw_note_column();
    tb_present();

    for (;;) {

        err = tb_poll_event(&event);
        if (err < 0) {
            tb_shutdown();
            die("event error\n");
        }

        if (event.type == TB_EVENT_RESIZE)
            goto full_redraw;

        if (event.type == TB_EVENT_MOUSE)
            continue;

        char this_note;

        if (!event.ch) {
            switch (event.key) {

            /* aliases for characters */
            case TB_KEY_ESC:
            case TB_KEY_CTRL_C:
                event.ch = 'q';
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
                    audio();
                    exit(0);
                }
                break;

            /* toggle between this note and no note */
            case TB_KEY_SPACE:
                this_note = pattern[current_line].note;

                if (this_note == 0) {
                    pattern[current_line].note = last_edit;
                    break;
                }

                last_edit = pattern[current_line].note;
                /* fallthrough */

            case TB_KEY_BACKSPACE2:
            case TB_KEY_DELETE:
                pattern[current_line].note = 0;
                break;
            }
        }

        switch (event.ch) {
        case 'q':
        case 'Q':
            tb_shutdown();
            return 0;
        case 'h':
            this_note = pattern[current_line].note;
            if (this_note == 0)
                pattern[current_line].note = last_edit;
            else if (this_note > 1)
                last_edit = --pattern[current_line].note;
            break;
        case 'j':
            current_line++;
            break;
        case 'k':
            current_line--;
            break;
        case 'l':
            this_note = pattern[current_line].note;
            if (this_note == 0)
                pattern[current_line].note = last_edit;
            else if (this_note < 63)
                last_edit = ++pattern[current_line].note;
            break;
        }

            if (current_line > 15)
                current_line = 0;
            else if (current_line < 0)
                current_line = 15;

        draw_note_column();
        tb_present();
    }
}
