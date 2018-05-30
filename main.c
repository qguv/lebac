/* le bac: the badge audio composer
 * all bugs are qguv's fault */

#include "notes.h"

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

#define TEMPO_MAX 200
#define TEMPO_MIN 20
#define TEMPO_JUMP 10

struct line_t {
    char note;
};

struct line_t pattern[16];

int current_line = 0;

int tempo = 128;

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

void audio(int audio_pipe)
{
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
    *octave = "234567"[(line->note - 1) / 12];
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
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    char s[4];
    snprintf(s, sizeof(s), "T:");
    tb_puts(s, &cell, 0, 1);

    cell.bg = TB_MAGENTA;
    snprintf(s, sizeof(s), "%3d", tempo);
    tb_puts(s, &cell, 3, 1);
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

    struct tb_cell cell;
    cell.fg = TB_DEFAULT;
    cell.bg = TB_DEFAULT;

    char quit_request = 0;
    char full_redraw = 1;

    for (;;) {
        if (full_redraw) {
            tb_clear();
            draw_tempo();
            tb_puts("le bac / the badge audio composer", &cell, 8, 1);
            full_redraw = 0;
        }

        draw_note_column();
        tb_present();

        err = tb_poll_event(&event);
        if (err < 0) {
            /* TODO handle more gracefully */
            tb_shutdown();
            die("event error\n");
        }

        if (event.type == TB_EVENT_RESIZE) {
            full_redraw = 1;
            continue;
        }

        if (event.type == TB_EVENT_MOUSE)
            continue;

        if (!event.ch) {
            switch (event.key) {

            /* aliases for characters */
            case TB_KEY_ESC:
            case TB_KEY_CTRL_C:
                event.ch = 'Q';
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

            case TB_KEY_BACKSPACE2:
            case TB_KEY_DELETE:
                pattern[current_line].note = 0;
                break;
            }
        }

        if (event.ch == 'Q') {

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
            full_redraw = 1;
            quit_request = 0;
        }

        switch (event.ch) {

        case 'C':
            for (int i = 0; i < 16; i++)
                pattern[i].note = 0;
            break;

        /* tap tempo */
        case 'T':
            clock_gettime(CLOCK_REALTIME, &tempo_input);

            if (last_tempo_input.tv_sec != 0) {
                double x = (double) (tempo_input.tv_sec & 0xffff) - (double) (last_tempo_input.tv_sec & 0xffff);
                x += (tempo_input.tv_nsec - last_tempo_input.tv_nsec) * 1e-9;
                tempo = 60.0L / x + 0.5;
                if (tempo < 20)
                    tempo = 20;
                if (tempo > 200)
                    tempo = 200;
                draw_tempo();
            }

            last_tempo_input.tv_sec = tempo_input.tv_sec;
            last_tempo_input.tv_nsec = tempo_input.tv_nsec;
            break;

        case 'H':
            if (pattern[current_line].note == 0)
                pattern[current_line].note = last_edit;

            if (pattern[current_line].note < 13)
                last_edit = pattern[current_line].note = 1;
            else
                last_edit = pattern[current_line].note -= 12;
            break;

        case 'h':
            if (pattern[current_line].note == 0)
                pattern[current_line].note = last_edit;

            if (pattern[current_line].note > 1)
                last_edit = --pattern[current_line].note;
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
            if (pattern[current_line].note == 0)
                pattern[current_line].note = last_edit;

            if (pattern[current_line].note < 63)
                last_edit = ++pattern[current_line].note;
            break;

        case 'L':
            if (pattern[current_line].note == 0)
                pattern[current_line].note = last_edit;

            if (pattern[current_line].note > 51) {
                last_edit = pattern[current_line].note = 63;
            } else {
                last_edit = pattern[current_line].note += 12;
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
            pattern[current_line].note = last_edit;
            break;
        }
    }
}
