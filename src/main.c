/* le bac: the badge audio composer
 * all bugs are qguv's fault */

#include "notes.h"
#include "help.h"
#include "wavehop.h"
#include "wavetable.h"

#include <termbox.h>

#include <fcntl.h> // open()
#include <stdint.h> // int16_t
#include <stdio.h> // snprintf, fputs (errors)
#include <stdlib.h> // exit
#include <time.h> // clock_gettime
#include <unistd.h> // pipe, fork, close, dup, exec, ...
#include <string.h> // strerror()
#include <errno.h> // errno
#include <stdarg.h>
#include <dirent.h> // readdir

/* badge audio rate */
#define RATE 38000

/* more mercy == less volume */
#define MERCY 3

#define TEMPO_MAX 255
#define TEMPO_MIN 16
#define TEMPO_JUMP 10

#define NOTE_MAX 97

#define QUANTIZE(X, MINVAL, MAXVAL) (\
    ((X) == 0) ? 0 : \
    ((X) > 0) ? (MAXVAL) : \
    (MINVAL) \
)

#define MAX(X, Y) (((Y) > (X)) ? (Y) : (X))
#define MIN(X, Y) (((Y) < (X)) ? (Y) : (X))

#define ALEN(X) (sizeof(X) / sizeof(X[0]))

const char magic[] = "badge18";

const char * const duties[] = {
    "!!",
    "50",
    "25",
    "12",
    " 6",
    " 3",
    " 1",
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

char filename[128];

struct page_t *page, *tmp_page;
int page_num = 1;
int num_pages = 1;

int current_line = 0;

unsigned char tempo = 128;

static int command_multiplier = 0;
static struct note_t *yank_buffer = NULL;
static int yank_buffer_size = 0;

/* crunch into 1.5 bit space? */
char emulate_shitty_badge_audio = 1;

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

void tb_printf(char *format, ...)
{
    int i;
    char buffer[80];
    va_list arg_ptr;

    /* Blank out the status line */
    for (i = 0; i < (int) sizeof(buffer) - 1; i++)
        buffer[i] = ' ';
    buffer[sizeof(buffer) - 1] = '\0';
    tb_puts(buffer, &dcell, 1, 21);

    /* Fill in the status line with the new error message */
    va_start(arg_ptr, format);
    vsnprintf(buffer, sizeof(buffer), format, arg_ptr);
    va_end(arg_ptr);
    tb_puts(buffer, &dcell, 1, 21);
}

int audio_child(int * const pid_p, const char * const filename)
{
    /* set up pipes to communicate with audio child */
    int pipefds[2];
    int err = pipe(pipefds);
    if (err) {
        fprintf(stderr, "Couldn't create a pipe: %s\n", strerror(errno));
        exit(1);
    }

    /* spin off a child to produce the audio */
    int pid = fork();
    if (pid == 0) {
        close(pipefds[1]);
        dup2(pipefds[0], STDIN_FILENO);
        close(pipefds[0]);
        if (filename) {
            execlp("out123", "out123", "--mono", "--encoding", "s16", "--rate", "38000", "--wav", filename, (char *) NULL);
            execlp("aplay", "aplay", "--quiet", "-c", "1", "-f", "S16_LE", "-r", "38000", filename, (char *) NULL);
        } else {
            execlp("out123", "out123", "--mono", "--encoding", "s16", "--rate", "38000", (char *) NULL);
            execlp("aplay", "aplay", "--quiet", "-c", "1", "-f", "S16_LE", "-r", "38000", (char *) NULL);
        }
    }
    if (pid_p != NULL)
        *pid_p = pid;

    close(pipefds[0]);
    return pipefds[1];
}

/* call audio with the output of audio_child to play audio. you need a new
 * audio_child pipe each time. we will close the audio pipe for you when it's
 * done playing.
 *
 * there are two such waves which are added to produce a result--this is what
 * is meant by "two-channel audio".
 *
 * if emulate_shitty_badge_audio, we'll then restrict the wave to
 * three possible values: INT16_MAX, 0, and INT16_MIN, representing the push/1,
 * neutralize/0, and pull/-1 states the H-bridge can take when driving the
 * buzzer on the badge. this gives you a very accurate simulation of how your
 * composition will sound when playing on the badge.
 */
void audio(int audio_pipe, char just_one_page)
{
    /* duration of one sequencer step, in samples */
    const int samples_per_step = RATE * 15 / tempo;

    /* position of each wave in its oscillation */
    float cycle_pos[2] = {0, 0};

    float hop[2] = {0, 0};

    /* whether the wave is currently playing */
    char play[2] = {0, 0};

    /* scroll back to the first page */
    struct page_t *playing_page = page;
    if (!just_one_page)
        while (playing_page->prev != NULL)
            playing_page = playing_page->prev;

    while (playing_page) {
        for (int step = 0; step < 16; step++) {
            for (int channel = 0; channel < 2; channel++) {

                char note = playing_page->notes[step][channel].note;
                /*
                TODO: use duty cycle as sin volume
                char duty = playing_page->notes[step][channel].duty;
                */

                if (note > 0) {
                    play[channel] = 1;
                    hop[channel] = wavehop_table[(int) note];
                    cycle_pos[channel] = 0;
                } else if (note < 0) {
                    play[channel] = 0;
                }
                /* else note == 0, so continue oscillating previous note */
            }

            for (int i = 0; i < samples_per_step; i++) {
                int16_t sample = 0;
                char lower_threshhold = 0;
                for (int channel = 0; channel < 2; channel++) {

                    if (!play[channel]) {
                        lower_threshhold = 1;
                        continue;
                    }

                    sample += wave_table[(int) cycle_pos[channel]];

                    cycle_pos[channel] += hop[channel];
                    if (cycle_pos[channel] >= ALEN(wave_table))
                        cycle_pos[channel] -= ALEN(wave_table);
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
                if (emulate_shitty_badge_audio) {
                    sample = lower_threshhold ? (
                        (sample < -15250) ? INT16_MIN >> MERCY :
                        (sample > 15250) ? INT16_MAX >> MERCY :
                        0
                    ) : (
                        (sample < -14000) ? INT16_MIN >> MERCY :
                        (sample > 14000) ? INT16_MAX >> MERCY :
                        0
                    );
                }

                write(audio_pipe, (char *) &sample, sizeof(sample));
            }
        }

        playing_page = just_one_page ? NULL : playing_page->next;
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
    bright.bg = TB_BLACK;

    struct tb_cell dark;
    dark.fg = TB_DEFAULT;
    dark.bg = TB_DEFAULT;

    struct tb_cell bright_bold;
    bright_bold.fg = TB_DEFAULT | TB_BOLD;
    bright_bold.bg = TB_BLACK;

    struct tb_cell dark_bold;
    dark_bold.fg = TB_DEFAULT | TB_BOLD;
    dark_bold.bg = TB_DEFAULT;

    struct tb_cell *cell;

    for (int row = 0; row < 16; row++) {

        const char is_quarter_note = (row % 4 == 0);
        const char is_current_line = (row == current_line);
        const char left_sel = is_current_line && selected_column == LEFT;
        const char right_sel = is_current_line && selected_column == RIGHT;

        cell = is_quarter_note
            ? is_current_line ? &bright_bold : &bright
            : is_current_line ? &dark_bold : &dark;

        /* note columns */
        for (int i = 0; i < 2; i++)
            tb_put_note(&page->notes[row][i], cell, 9 * i + 3, row + 4);

        dcell.ch = (left_sel) ? '-' : "0123456789abcdef"[row];
        tb_put_cell(0, row + 4, &dcell);

        /* arrow blits over line number */
        dcell.ch = (left_sel) ? '>' : ' ';
        tb_put_cell(1, row + 4, &dcell);

        /* right arrow */
        dcell.ch = (right_sel) ? '<': ' ';
        tb_put_cell(20, row + 4, &dcell);
        dcell.ch = (right_sel) ? '-': ' ';
        tb_put_cell(21, row + 4, &dcell);
    }
}

void draw_tempo(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    char s[4];
    snprintf(s, sizeof(s), "%3d", tempo);
    tb_puts(s, &cell, 3, 2);

    tb_puts("bpm", &cell, 7, 2);
}

void draw_page_num(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    char s[8];
    snprintf(s, sizeof(s), "%02x / %02x", page_num, num_pages);
    tb_puts(s, &cell, 12, 2);
}

void draw_emulated(void)
{
    struct tb_cell cell;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    tb_puts(emulate_shitty_badge_audio ? "EMU  ON" : "EMU off", &cell, 3, 1);
}

void draw_help(void)
{
    struct tb_cell cell;
    int col = 0;
    int row = 0;
    cell.fg = TB_DEFAULT | TB_BOLD;
    cell.bg = TB_DEFAULT;

    for (int i = 0; helptext[i][0]; i++) {
        if (strcmp(helptext[i][1], "") == 0)
           col = 0;
        cell.fg = TB_DEFAULT | TB_BOLD;
        tb_puts(helptext[i][0], &cell, 1 + 40 * col, row + 1);

        cell.fg = TB_DEFAULT;
        tb_puts(helptext[i][1], &cell, 8 + 40 * col, row + 1);
        if (strcmp(helptext[i][1], "") != 0)
           col = !col;
        if (!col)
           row++;
    }
}

void draw_not_quit(void)
{
    tb_printf("The quit key is ctrl-c");
}

static void bump_lines_up_one(struct page_t *page, int line_within_page)
{
    for (int i = 15; i > line_within_page; i--) {
        page->notes[i][0] = page->notes[i - 1][0];
        page->notes[i][1] = page->notes[i - 1][1];
    }

    memset(&page->notes[line_within_page], 0, sizeof(page->notes[0]));
}

static void delete_line(struct page_t *page, int line_within_page)
{
    /* Delete the line from this page, bumping down subsequent lines from this page */
    for (int i = line_within_page; i < 15; i++) {
        page->notes[i][0] = page->notes[i + 1][0];
        page->notes[i][1] = page->notes[i + 1][1];
    }

    /* pull in a note from the next page */
    if (page->next) {

        /* copy the first line from next page to last line of this page */
        page->notes[15][0] = page->next->notes[0][0];
        page->notes[15][1] = page->next->notes[0][1];

        /* Delete first line of next page, recursively */
        delete_line(page->next, 0);

    /* otherwise just blank it */
    } else {
        memset(&page->notes[15], 0, sizeof(page->notes[15]));
    }
}

static void insert_line(struct page_t *page, int line_within_page)
{
    /* make room on subsequent pages, if any */
    if (page->next) {

        insert_line(page->next, 0);

        /* Copy last note of this page to first note of next page. */
        page->next->notes[0][0] = page->notes[15][0];
        page->next->notes[0][1] = page->notes[15][1];

    /* otherwise it's the last page */
    } else {

        /* Check if the last note on this page is unused. */
        struct note_t *n = &page->notes[15][0];
        if (n[0].note != 0 || n[0].duty != 0 || n[1].note != 0 || n[1].duty != 0) {

            /* last note is used, so make a new page */
            page->next = calloc(1, sizeof(struct page_t));
            page->next->prev = page;
            num_pages++;

            /* Copy last note of this page to first note of next page. */
            page->next->notes[0][0] = page->notes[15][0];
            page->next->notes[0][1] = page->notes[15][1];
        }
    }

    /* Bump everything on this page up one */
    bump_lines_up_one(page, line_within_page);
}

static void yank_lines(struct page_t *page, int line_within_page, int count,
    struct note_t **yank_buf, int *yank_buf_size)
{
    int from, to, lines_copied = 0;
    if (*yank_buf)
        free(*yank_buf);
    *yank_buf = calloc(1, sizeof(**yank_buf) * count * 2);
    from = line_within_page;
    for (to = 0; to < count * 2; to += 2) {
        (*yank_buf)[to].note = page->notes[from][0].note;
        (*yank_buf)[to].duty = page->notes[from][0].duty;
        (*yank_buf)[to + 1].note = page->notes[from][1].note;
        (*yank_buf)[to + 1].duty = page->notes[from][1].duty;
        from++;
        lines_copied++;
        if (from > 15) {
            from = 0;
            page = page->next;
            if (!page)
                break;
        }
    }
    *yank_buf_size = lines_copied;
    return;
}

static void insert_lines(struct page_t *page, int line_within_page, int count)
{
    if (count == 0)
        count = 1;

    for (int i = 0; i < count; i++)
        insert_line(page, line_within_page);
}

static void delete_lines(struct page_t *page, int line_within_page, int count)
{
    if (count == 0)
        count = 1;

    yank_lines(page, line_within_page, count, &yank_buffer, &yank_buffer_size);
    for (int i = 0; i < count; i++)
        delete_line(page, line_within_page);
}

static void do_yank_lines(struct page_t *page, int line_within_page, int count)
{
    if (count == 0)
        count = 1;
    yank_lines(page, line_within_page, count, &yank_buffer, &yank_buffer_size);
    tb_printf("Yanked %d %s", count, count == 1 ? "line" : "lines");
}

static void paste_lines(struct page_t *page, int line_within_page, int count)
{
    int total = 0;

    if (count == 0)
       count = 1;
    if (yank_buffer_size == 0 || !yank_buffer) {
        tb_printf("0 lines pasted");
        return;
    }
    for (int i = 0; i < count; i++) {
        /* Make room for the new lines */
        insert_lines(page, line_within_page, yank_buffer_size);
        /* Copy the yank buffer into the new lines */
        for (int j = 0; j < yank_buffer_size * 2; j += 2) {
            page->notes[line_within_page][0].note = yank_buffer[j].note;
            page->notes[line_within_page][0].duty = yank_buffer[j].duty;
            page->notes[line_within_page][1].note = yank_buffer[j + 1].note;
            page->notes[line_within_page][1].duty = yank_buffer[j + 1].duty;
            line_within_page++;
            total++;
            if (line_within_page > 15) { /* Next page? */
                if (!page->next) { /* There is no next page, so make one. */
                   page->next = calloc(1, sizeof(*page->next));
                   page->next->prev = page;
                   page->next->next = NULL;
                   num_pages++;
                }
                page = page->next;
                line_within_page = 0;
            }
        }
    }
    tb_printf("Pasted %d %s", total, total == 1 ? "line" : "lines");
}

void save(char *songfile)
{
    /* TODO allow filenames to be specified at load */

    int fd = open(songfile, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) {
        tb_printf("%s: Can't open file: %s\n", songfile, strerror(errno));
        return;
    }

    /* rewind to the first page */
    struct page_t *saving_page = page;
    while (saving_page->prev != NULL)
        saving_page = saving_page->prev;

    write(fd, &magic, sizeof(magic));
    write(fd, &tempo, 1);
    write(fd, &emulate_shitty_badge_audio, 1);

    while (saving_page) {
        int written = 0;
        int wrote = 0;
        int to_write = sizeof(saving_page->notes);
        while (written < to_write) {
            wrote = write(fd, ((char *) saving_page->notes) + written, to_write - written);
            if (wrote < 0) {
                if (errno == EINTR)
                    continue;
                tb_printf("%s: write failed: %s", songfile, strerror(errno));
                break;
            }

            written += wrote;
        }

        saving_page = saving_page->next;
    }

    close(fd);
    tb_printf("saved to %s", songfile);
}

void load(char *songfile)
{
    /* TODO allow filenames to be specified at load */
    int fd = open(songfile, O_RDONLY);
    if (fd < 0) {
        tb_printf("%s: Cannot open: %s", songfile, strerror(errno));
        return;
    }

    struct page_t *load_page = NULL;
    int num_load_pages = 0;

    unsigned char peek;
    int peek_ret;

    for (int i = 0; i < (int) sizeof(magic); i++) {
        peek_ret = read(fd, &peek, 1);
        if (peek_ret != 1 || peek != magic[i]) {
            close(fd);
            tb_printf("load failed: bad magic");
            return;
        }
    }

    peek_ret = read(fd, &peek, 1);
    if (peek_ret != 1) {
        close(fd);
        tb_printf("load failed: bad tempo");
        return;
    }
    tempo = peek;

    peek_ret = read(fd, &peek, 1);
    if (peek_ret != 1 || (peek != 0 && peek != 1)) {
        close(fd);
        tb_printf("load failed: bad emulate_shitty_badge_audio");
        return;
    }
    emulate_shitty_badge_audio = peek;

    for (;;) {

        /* peek into the file to see if we need to allocate a whole 'nother page */
        peek_ret = read(fd, &peek, 1);

        /* read error; free and abort */
        if (peek_ret < 0) {
            if (errno != 0)
                tb_printf("load failed: %s", strerror(errno));
            break;

        } else if (peek_ret == 0) {

            /* didn't read anything */
            if (num_load_pages == 0) {
                tb_printf("load failed: file ended abruptly", songfile);
                break;
            }

            /* file ended normally; replace current pattern with loaded pattern */
            close(fd);

            /* free existing pages of pattern memory */
            while (page->prev)
                page = page->prev;
            while (page) {
                tmp_page = page->next;
                free(page);
                page = tmp_page;
            }

            /* load and seek to first page */
            page = load_page;
            while (page->prev)
                page = page->prev;
            page_num = 1;
            num_pages = num_load_pages;

            tb_printf("loaded %s", songfile);
            return;
        }

        /* allocate new page to hold more pattern */
        tmp_page = load_page;
        load_page = calloc(1, sizeof(struct page_t));
        load_page->prev = tmp_page;
        if (tmp_page)
            tmp_page->next = load_page;
        num_load_pages++;

        /* throw the peek byte in there */
        ((char *) load_page->notes)[0] = peek;

        /* load the rest with normal read calls */
        int just_red, total_red = 1, to_read = sizeof(load_page->notes);
        while (total_red < to_read) {
            just_red = read(fd, ((char *) load_page->notes) + total_red, to_read - total_red);
            if (just_red < 0 && errno == EINTR)
                continue;

            /* file didn't end at a page boundary; free and abort */
            if (just_red <= 0) {
                tb_printf("failed: file didn't end on a page boundary");
                break;
            }

            total_red += just_red;
        }
    }

    /* we get here on read errors */
    close(fd);
    while (load_page) {
        tmp_page = load_page->prev;
        free(load_page);
        load_page = tmp_page;
    }
}

void complete_filename(void)
{
    /* keep track of the area we've written to for clearing next round */
    static int xpos_max = 2;
    static int ypos = 22;

    /* clear completion area */
    dcell.ch = ' ';
    for (int x = 2; x < xpos_max; x++)
        for (int y = 22; y < ypos + 1; y++)
            tb_put_cell(x, y, &dcell);

    int xpos = 2;
    ypos = 22;

    /* find the last directory */
    int last_slash = -1;
    char c;
    for (int i = 0; i < (int) sizeof(filename) && (c = filename[i]); i++)
        if (c == '/')
            last_slash = i;

    char dirname[128] = ".";
    const char *leaf = filename;
    if (last_slash > 0) {
        strncpy(dirname, filename, MIN(last_slash + 1, (int) sizeof(dirname)));
        leaf = filename + last_slash + 1;
    }

    DIR *d;
    struct dirent *e;
    d = opendir(dirname);

    if (d) {
        char the_only_entry[128];

        int entries = 0;
        while ((e = readdir(d))) {

            if (e->d_name[0] == '.')
                continue;

            if (strncmp(leaf, e->d_name, strlen(leaf)) != 0)
                continue;

            if (xpos > 40) {
                xpos_max = MAX(xpos_max, xpos);
                ypos++;
                xpos = 2;
            }

            dcell.ch = ' ';
            tb_put_cell(xpos++, ypos, &dcell);

            const char *s = e->d_name;
            char c;
            while ((c = *(s++))) {
                dcell.ch = c;
                tb_put_cell(xpos++, ypos, &dcell);
            }

            if (e->d_type == DT_DIR) {
                dcell.ch = '/';
                tb_put_cell(xpos++, ypos, &dcell);
            }

            entries++;

            if (entries == 1) {
                strncpy(the_only_entry, e->d_name, sizeof(the_only_entry));
                if (e->d_type == DT_DIR)
                    strcat(the_only_entry, "/");
            }
        }

        closedir(d);

        if (entries == 1)
            strncpy(filename + last_slash + 1, the_only_entry, sizeof(filename) - last_slash - 1);
    }
}

void clear_filename(void)
{
    for (int i = 0; i < (int) sizeof(filename); i++)
        filename[i] = '\0';
}

/* returns 1 on error */
char get_filename(const char * const prompt)
{
    /* we get our own event loop, since we hijack user input */
    for (;;) {
        int xpos = 1;

        char c;
        const char *s = prompt;
        while ((c = *(s++))) {
            dcell.ch = c;
            tb_put_cell(xpos++, 21, &dcell);
        }

        dcell.ch = ':';
        tb_put_cell(xpos++, 21, &dcell);

        dcell.ch = ' ';
        tb_put_cell(xpos++, 21, &dcell);

        int i;
        for (i = 0; (c = filename[i]); i++) {
            dcell.ch = c;
            tb_put_cell(xpos++, 21, &dcell);
        }

        tb_present();

        struct tb_event event;
        int err = tb_poll_event(&event);
        if (err < 0 || event.type != TB_EVENT_KEY) {
            continue;
        }

        if (event.ch) {
            filename[i] = event.ch;

        } else switch (event.key) {

            case TB_KEY_ESC:
            case TB_KEY_CTRL_C:
                return 1;

            case TB_KEY_BACKSPACE:
            case TB_KEY_BACKSPACE2:
                dcell.ch = ' ';
                tb_put_cell(xpos - 1, 21, &dcell);
                filename[i - 1] = 0;
                continue;

            case TB_KEY_ENTER:
                return 0;

            case TB_KEY_CTRL_A:
                clear_filename();
                continue;

            case TB_KEY_TAB:
                complete_filename();
                continue;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                fprintf(stderr, "Usage: lebac [songfile]\n");
                exit(0);
            }
        }
    }

    char cli_load_song = argc > 1;
    if (cli_load_song) {
        char c, *s = argv[1];
        for (int i = 0; i < (int) sizeof(filename) && (c = *s); s++)
            filename[i] = c;
    }

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

    clear_filename();

    for (;;) {

        if (redraw_setting == FULL) {
            tb_clear();

            if (cli_load_song) {
                cli_load_song = 0;
                load(argv[1]);
            }

            if (global_mode == SEQUENCER) {
                draw_page_num();
                draw_tempo();
                draw_emulated();
            } else if (global_mode == HELP) {
                draw_help();
            }
        }

        if (global_mode == SEQUENCER) {
            draw_note_columns(column);
            edit_note = (column == LEFT) ? &page->notes[current_line][0] : &page->notes[current_line][1];
        }

        tb_present();

        redraw_setting = NORMAL;

        err = tb_poll_event(&event);
        if (err < 0) {
            tb_printf("termbox event error :(  ");
            continue;
        }

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
            tb_printf("press again to quit     ");
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
            case TB_KEY_ENTER:
                event.ch = 'p';
                break;

            /* switch columns */
            case TB_KEY_TAB:
                column = (column == LEFT) ? RIGHT : LEFT;
                break;

            /* delete this note */
            case TB_KEY_BACKSPACE:
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

        /* play whole pattern */
        case 'P':
            if (!fork()) {
                /* TODO: send SIGTERM when parent dies */
                int audio_pipe = audio_child(NULL, NULL);
                audio(audio_pipe, 0);
                exit(0);
            }
            break;

        /* export song to wavfile */
        case 'W':
            err = get_filename("export wav to");
            if (err) {
                tb_printf("wav export cancelled");
                break;
            }
            tb_printf("exported to %s", filename);
            if (!fork()) {
                /* TODO: send SIGTERM when parent dies */
                int audio_pipe = audio_child(NULL, filename);
                audio(audio_pipe, 0);
                exit(0);
            }
            break;

        /* play this page */
        case 'p':
            if (!fork()) {
                /* TODO: send SIGTERM when parent dies */
                int audio_pipe = audio_child(NULL, NULL);
                audio(audio_pipe, 1);
                exit(0);
            }
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
                draw_page_num();
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
                    draw_page_num();
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
            draw_page_num();
            break;

        /* prev page */
        case 'K':
            if (page_num > 1) {
                page = page->prev;
                page_num--;
                draw_page_num();
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
                num_pages--;

                /* introduce the neighbors to each other */
                if (page->next)
                    page->next->prev = page->prev;
                if (page->prev)
                    page->prev->next = page->next;

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

                draw_page_num();
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

        case '[':
            if (edit_note->duty < 6)
                edit_note->duty++;
            last_edit.note = edit_note->note;
            last_edit.duty = edit_note->duty;
            break;

        case ']':
            if (edit_note->duty > 1)
                edit_note->duty--;
            last_edit.note = edit_note->note;
            last_edit.duty = edit_note->duty;
            break;

        case '.':
            edit_note->note = last_edit.note;
            edit_note->duty = last_edit.duty;
            break;

        case 'E':
            emulate_shitty_badge_audio = !emulate_shitty_badge_audio;
            draw_emulated();
            break;

        case '?':
            global_mode = HELP;
            redraw_setting = FULL;
            break;

        case 'S':
            err = get_filename("save to");
            if (err) {
                tb_printf("save cancelled");
                break;
            }
            save(filename);
            break;

        case 'i':
            insert_lines(page, current_line, command_multiplier);
            draw_page_num();
            tb_printf("Inserted %d new %s", command_multiplier, command_multiplier == 1 ? "line" : "lines");
            command_multiplier = 0;
            break;

        case 'd':
            delete_lines(page, current_line, command_multiplier);
            draw_page_num();
            tb_printf("Deleted %d %s", command_multiplier, command_multiplier == 1 ? "line" : "lines");
            command_multiplier = 0;
            break;

        case 'v':
            paste_lines(page, current_line, command_multiplier);
            command_multiplier = 0;
            draw_page_num();
            break;

        case 'c':
            do_yank_lines(page, current_line, command_multiplier);
            command_multiplier = 0;
            break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            command_multiplier = 10 * command_multiplier + event.ch - '0';
            break;

        case 'D':
            err = get_filename("load from");
            if (err) {
                tb_printf("load cancelled");
                break;
            }
            load(filename);
            draw_page_num();
            draw_tempo();
            draw_emulated();
            break;
        }
    }
}
