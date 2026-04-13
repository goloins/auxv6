#ifndef AUXV6_TTY_LDISC_H
#define AUXV6_TTY_LDISC_H

#include "types.h"
#include "termios.h"
#include "tty.h"

struct tty_ldisc_state {
  char canon_buf[TTY_LDISC_CANON_BUFSZ];
  int canon_len;
  int out_col;
  int flow_stopped;
};

void tty_ldisc_init(struct tty_ldisc_state *ld);
void tty_ldisc_reset(struct tty_ldisc_state *ld);

/*
 * Process bytes written by PTY master (keyboard side) into slave input queue.
 * Returns number of bytes produced for slave input queue.
 * Echo bytes for the master side are returned via echo_out/echo_cap.
 */
int tty_ldisc_process_input(const struct termios *tp,
                            struct tty_ldisc_state *ld,
                            const char *in,
                            int in_len,
                            char *slave_out,
                            int slave_cap,
                            char *echo_out,
                            int echo_cap,
                            int *echo_len,
                            int *sig_out);

/*
 * Process bytes written by PTY slave (program output) into master queue.
 * Returns number of bytes produced for master output queue.
 */
int tty_ldisc_process_output(const struct termios *tp,
                             struct tty_ldisc_state *ld,
                             const char *in,
                             int in_len,
                             char *master_out,
                             int master_cap);

#endif