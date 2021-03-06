/****************************************************************************
 * Copyright (c) 1998 Free Software Foundation, Inc.                        *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/



/*
**	lib_newterm.c
**
**	The newterm() function.
**
*/

#include <curses.priv.h>

#if defined(SVR4_TERMIO) && !defined(_POSIX_SOURCE)
#define _POSIX_SOURCE
#endif

#if	defined(__QNX__) && !defined(__QNXNTO__)
#include <sys/dev.h>
#endif

#include <term.h>	/* clear_screen, cup & friends, cur_term */

MODULE_ID("$Id: lib_newterm.c 153052 2007-11-02 21:10:56Z coreos $")

#ifndef ONLCR		/* Allows compilation under the QNX 4.2 OS */
#define ONLCR 0
#endif

/*
 * SVr4/XSI Curses specify that hardware echo is turned off in initscr, and not
 * restored during the curses session.  The library simulates echo in software.
 * (The behavior is unspecified if the application enables hardware echo).
 *
 * The newterm function also initializes terminal settings, and since initscr
 * is supposed to behave as if it calls newterm, we do it here.
 */
static inline int _nc_initscr(void)
{
	/* for extended XPG4 conformance requires cbreak() at this point */
	/* (SVr4 curses does this anyway) */
	cbreak();

#ifdef TERMIOS
	cur_term->Nttyb.c_lflag &= ~(ECHO|ECHONL);
	cur_term->Nttyb.c_iflag &= ~(ICRNL|INLCR|IGNCR);
	cur_term->Nttyb.c_oflag &= ~(ONLCR);
#else
	cur_term->Nttyb.sg_flags &= ~(ECHO|CRMOD);
#endif
	return _nc_set_curterm(&cur_term->Nttyb);
}

/*
 * filter() has to be called before either initscr() or newterm(), so there is
 * apparently no way to make this flag apply to some terminals and not others,
 * aside from possibly delaying a filter() call until some terminals have been
 * initialized.
 */
static int filter_mode = FALSE;

void filter(void)
{
    filter_mode = TRUE;
}

SCREEN * newterm(NCURSES_CONST char *term, FILE *ofp, FILE *ifp)
{
int	errret;
SCREEN* current;
#ifdef TRACE
char *t = getenv("NCURSES_TRACE");

	if (t)
               trace((unsigned) strtol(t, 0, 0));
#endif

	T((T_CALLED("newterm(\"%s\",%p,%p)"), term, ofp, ifp));

	/* this loads the capability entry, then sets LINES and COLS */
	if (setupterm(term, fileno(ofp), &errret) == ERR)
		return 0;

	/*
	 * Check for mismatched graphic-rendition capabilities.  Most SVr4
	 * terminfo trees contain entries that have rmul or rmso equated to
	 * sgr0 (Solaris curses copes with those entries).  We do this only for
	 * curses, since many termcap applications assume that smso/rmso and
	 * smul/rmul are paired, and will not function properly if we remove
	 * rmso or rmul.  Curses applications shouldn't be looking at this
	 * detail.
	 */
	if (exit_attribute_mode) {
#define SGR0_FIX(mode) if (mode != 0 && !strcmp(mode, exit_attribute_mode)) \
			mode = 0
		SGR0_FIX(exit_underline_mode);
		SGR0_FIX(exit_standout_mode);
	}

#if	defined(__QNX__) || defined(__QNXNTO__)
	/* The qnx/qnxm term need to fix these strings
	 *		setf: \E@%p1%Pf%gb%gf%d%d -> \E@%p1%PF%gB%gF%d%d
	 *		setb: \E@%p1%Pb%gb%gf%d%d -> \E@%p1%PB%gB%gF%d%d
	 *      sgr:  this "set_attributes" only set, but not unset attributs.
	 *            force it to NULL, so vidputs() will use TURN_ON/OFF.
	 */
	if (term && !strnicmp(term, "qnx", 3)) {
		if (set_foreground && !strcmp(set_foreground, "\x1b@%p1%Pf%gb%gf%d%d"))
			set_foreground[7] = 'F', set_foreground[10] = 'B', set_foreground[13] = 'F';
		if (set_background && !strcmp(set_background, "\x1b@%p1%Pb%gb%gf%d%d"))
			set_background[7] = 'B', set_background[10] = 'B', set_background[13] = 'F';
		set_attributes = 0;
	}
#endif

	/* implement filter mode */
	if (filter_mode) {
		LINES = 1;

#ifdef init_tabs
		if (init_tabs != -1)
			TABSIZE = init_tabs;
		else
#endif /* init_tabs */
			TABSIZE = 8;

		T(("TABSIZE = %d", TABSIZE));

#ifdef clear_screen
		clear_screen = 0;
		cursor_down = parm_down_cursor = 0;
		cursor_address = 0;
		cursor_up = parm_up_cursor = 0;
		row_address = 0;

		cursor_home = carriage_return;
#endif /* clear_screen */
	}

	/* If we must simulate soft labels, grab off the line to be used.
	   We assume that we must simulate, if it is none of the standard
	   formats (4-4  or 3-2-3) for which there may be some hardware
	   support. */
#ifdef num_labels
	if (num_labels <= 0 || !SLK_STDFMT)
#endif /* num_labels */
	    if (_nc_slk_format)
	      {
		if (ERR==_nc_ripoffline(-SLK_LINES, _nc_slk_initialize))
		  return 0;
	      }
	/* this actually allocates the screen structure, and saves the
	 * original terminal settings.
	 */
	current = SP;
	_nc_set_screen(0);
	if (_nc_setupscreen(LINES, COLS, ofp) == ERR) {
	        _nc_set_screen(current);
		return 0;
	}

#ifdef num_labels
	/* if the terminal type has real soft labels, set those up */
	if (_nc_slk_format && num_labels > 0 && SLK_STDFMT)
	    _nc_slk_initialize(stdscr, COLS);
#endif /* num_labels */

	SP->_ifd        = fileno(ifp);
	SP->_checkfd	= fileno(ifp);
	typeahead(fileno(ifp));
#ifdef TERMIOS
	SP->_use_meta   = ((cur_term->Ottyb.c_cflag & CSIZE) == CS8 &&
			    !(cur_term->Ottyb.c_iflag & ISTRIP));
#else
	SP->_use_meta   = FALSE;
#endif
	SP->_endwin	= FALSE;

	/* Check whether we can optimize scrolling under dumb terminals in case
	 * we do not have any of these capabilities, scrolling optimization
	 * will be useless.
	 */
	SP->_scrolling = ((scroll_forward && scroll_reverse) ||
			  ((parm_rindex || parm_insert_line || insert_line) &&
			   (parm_index  || parm_delete_line || delete_line)));

	baudrate();	/* sets a field in the SP structure */

	SP->_keytry = 0;

#if	defined(__QNX__) && !defined(__QNXNTO__)
	SP->_qnx_kbd_proxy = -1;
	SP->_qnx_kbd_vproxy = -1;
	SP->_qnx_timer_proxy = -1;
	SP->_qnx_timer = -1;
	SP->_qnx_mouse_proxy = -1;
	SP->_qnx_mouse_ctrl = NULL;
	SP->_qnx_mouse_armed = -1;
	SP->_qnx_user_callback = NULL;
	/* The +opost in QNX terminal mess up the optimization */
	if (!strnicmp(term, "qnx", 3))
		cur_term->Nttyb.c_oflag &= ~(OPOST);
#endif

	/* compute movement costs so we can do better move optimization */
	_nc_mvcur_init();

	_nc_signal_handler(TRUE);

	/* initialize terminal to a sane state */
	_nc_screen_init();

	/* Initialize the terminal line settings. */
	_nc_initscr();

	T((T_RETURN("%p"), SP));
	return(SP);
}
