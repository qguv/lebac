/* moves s to the character after the last slash and returns the number of
 * characters between the slash and the next dot */
int varname_from_filename(const char **instr)
{
    int last_slash = -1, last_dot = -1;

    char c;
    int i;
    for (i = 0; (c = (*instr)[i]); i++) {
        switch (c) {
        case '/':
            last_slash = i;
            last_dot = -1;
            break;
        case '.':
            last_dot = i;
            break;
        }
    }

    if (last_dot == -1)
        last_dot = i;

    *instr += last_slash + 1;
    return last_dot - last_slash - 1;
}

