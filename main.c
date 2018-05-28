/* le bac: the badge audio composer
 * all bugs are qguv's fault
 * requires libout123 (mpg123) and termbox (included) */

#include <out123.h>

#include <stdio.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <termbox.h>
#include <unistd.h>

/* set only if your audio driver needs tweaking */
#define DRIVER NULL
#define ENCODING "s16";

/* badge audio rate */
#define RATE 38000L

struct line_t {
    char note;
};

struct line_t pattern[16];

int current_line = 0;

void die(const char * const s)
{
    fputs(s, stderr);
    exit(1);
}

void tb_puts(int x, int y, const char *s)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT;
    cell.bg = TB_DEFAULT;

    char c;
    for (int i = 0; (c = s[i]); i++) {
        cell.ch = c;
        tb_put_cell(x + i, y, &cell);
    }
}

int audio(void)
{
    out123_handle *ao = NULL;

    /* s16 since it's the cheapest, but we're really only using INT16_MAX, 0,
     * and INT16_MIN */
    const char *encname = ENCODING
    const int channels = 1;

    int mercy = 3;

    char *driver = DRIVER;

    int16_t *buffer = NULL;

    /* initialize out123 */
    ao = out123_new();
    if (!ao) {
        fprintf(stderr, "out123_new() died, is audio set up on this system?\n");
        out123_del(ao);
        return 1;
    }

    /* check that the encoding is supported */
    int encid = out123_enc_byname(encname);
    if (!encid) {
        fprintf(stderr, "encoding %s not supported by the default audio driver on this system. choose a better one!\n", encname);
        out123_del(ao);
        return 1;
    }

    int err = out123_open(ao, driver, NULL);
    if (err) {
        fprintf(stderr, "out123_open() died, saying \"%s\"\n", out123_strerror(ao));
        out123_del(ao);
        return 1;
    }

    out123_driver_info(ao, &driver, NULL);
    printf("Effective output driver: %s\n", driver ? driver : "<nil> (default)");
    printf("Playing with %i channels and %li Hz, encoding %s.\n", channels, RATE, encname ? encname : "???");

    int framesize;
    if (out123_start(ao, RATE, channels, encid) || out123_getformat(ao, NULL, NULL, NULL, &framesize)) {
        fprintf(stderr, "Cannot start output / get framesize: %s\n", out123_strerror(ao));
        out123_del(ao);
        return 1;
    }
    fprintf(stderr, "Framesize is %d\n", framesize);

    buffer = malloc(RATE);

    off_t samples = 0;
    do {
        long wavelength = RATE / 440;
        long duty = wavelength >> 1;
        for (int i = 0; i < duty; i++)
            buffer[i] = INT16_MAX >> mercy;
        for (int i = duty; i < wavelength; i++)
            buffer[i] = INT16_MIN >> mercy;

        size_t bytes_to_play = wavelength * framesize;
        size_t played = out123_play(ao, buffer, bytes_to_play);
        if (played != bytes_to_play)
        {
            fprintf(stderr, "Warning: some buffer remains after writing: %li != %li\n", (long) played, (long) bytes_to_play);
        }

        samples += played / framesize;
    } while (samples < RATE * 5);

    free(buffer);

    printf("%li samples written.\n", (long) samples);
    out123_del(ao);
    return 0;
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
    *octave = "1234567"[line->note / 12];
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

        black.ch = (row == current_line) ? '-' : ' ';
        tb_put_cell(0, row + 3, &black);
        black.ch = (row == current_line) ? '>' : ' ';
        tb_put_cell(1, row + 3, &black);
    }
}

int main(void)
{
    int err = tb_init();
    if (err) {
        die("Termbox failed to initialize\n");
    }

    if (tb_width() < 80 || tb_height() < 24) {
        tb_shutdown();
        die("Too small--we need at least 80x24\n");
    }

    char last_edit = 25;

full_redraw:
    tb_clear();
    tb_puts(3, 1, "le bac: the badge audio composer");
    draw_note_column();
    tb_present();

    for (;;) {

        struct tb_event event;
        int err = tb_poll_event(&event);
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
                if (!fork())
                    exit(audio());
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
