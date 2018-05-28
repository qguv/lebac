/*
   mpg123_to_wav.c

   copyright 2007-2016 by the mpg123 project - free software under the terms of the LGPL 2.1
   see COPYING and AUTHORS files in distribution or http://mpg123.org
   initially written by Nicholas Humfrey

   The most complicated part is about the choices to make about output format,
   and prepare for the unlikely case a bastard mp3 might file change it.
   */

#include <out123.h>
#include <stdio.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <termbox.h>
#include <unistd.h>

out123_handle *ao = NULL;

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
    char *infile = NULL;

    const char *encname = "s16";
    const long rate = 38000;
    const int channels = 1;

    int volshift = 3;

    /* encoding-specific buffer */
    int16_t *buffer = NULL;
    const size_t buffer_size = 38000;

    /* try changing your default audio drivers and devices before you fuck
     * around with these -- you shouldn't have to unless your system is really
     * exotic. */
    char *driver = NULL;
    char *outfile = NULL;

    printf("Input file: %s\n", infile);
    printf("Output driver: %s\n", driver ? driver : "<nil> (default)");
    printf("Output file: %s\n", outfile ? outfile : "<nil> (default)");

    ao = out123_new();
    if (!ao) {
        fprintf(stderr, "Cannot create output handle.\n");
        out123_del(ao);
        return -1;
    }

    /* If that is zero, you'll get the error soon enough from mpg123. */
    int encoding = out123_enc_byname(encname);
    if (!encoding) {
        fprintf(stderr, "No such encoding %s!\n", encname);
        out123_del(ao);
        return -1;
    }

    int err = out123_open(ao, driver, outfile);
    if (err) {
        fprintf(stderr, "Trouble with out123: %s\n", out123_strerror(ao));
        out123_del(ao);
        return -1;
    }

    /* It makes no sense for that to give an error now. */
    out123_driver_info(ao, &driver, &outfile);
    printf("Effective output driver: %s\n", driver ? driver : "<nil> (default)");
    printf("Effective output file: %s\n", outfile ? outfile : "<nil> (default)");

    encname = out123_enc_name(encoding);
    printf( "Playing with %i channels and %li Hz, encoding %s.\n", channels, rate, encname ? encname : "???" );

    int framesize;
    if (out123_start(ao, rate, channels, encoding) || out123_getformat(ao, NULL, NULL, NULL, &framesize)) {
        fprintf(stderr, "Cannot start output / get framesize: %s\n", out123_strerror(ao));
        out123_del(ao);
        return -1;
    }
    fprintf(stderr, "Framesize is %d\n", framesize);

    buffer = malloc(buffer_size);

    off_t samples = 0;
    do {
        long wavelength = rate / 440;
        long duty = wavelength >> 1;
        for (int i = 0; i < duty; i++)
            buffer[i] = INT16_MAX >> volshift;
        for (int i = duty; i < wavelength; i++)
            buffer[i] = INT16_MIN >> volshift;

        size_t bytes_to_play = wavelength * framesize;
        size_t played = out123_play(ao, buffer, bytes_to_play);
        if (played != bytes_to_play)
        {
            fprintf(stderr, "Warning: some buffer remains after writing: %li != %li\n", (long) played, (long) bytes_to_play);
        }

        samples += played / framesize;
    } while (samples < rate * 5);

    free(buffer);

    printf("%li samples written.\n", (long) samples);
    out123_del(ao);
    return 0;
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

    int line = 0, col = 0;

    tb_present();

    for (;;) {

        struct tb_event event;
        int err = tb_poll_event(&event);
        if (err < 0) {
            tb_shutdown();
            die("event error\n");
        }

        if (event.type != TB_EVENT_KEY)
            continue;

        if (!event.ch) {
            switch (event.key) {
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
            }
        }

        switch (event.ch) {
        case 'q':
        case 'Q':
            tb_shutdown();
            return 0;
        case 'h':
            col--;
            if (col < 0)
                col = 0;
            break;
        case 'j':
            line++;
            break;
        case 'k':
            line--;
            if (line < 0)
                line = 0;
            break;
        case 'l':
            col++;
            break;
        }


        tb_puts(col, line, "le bac: the badge audio composer");
        tb_present();
    }
}
