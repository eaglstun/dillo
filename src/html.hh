/*
 * File: html.hh
 *
 * Copyright (C) 2005-2009 Jorge Arellano Cid <jcid@dillo.org>
 * Copyright (C) 2024-2026 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef __HTML_HH__
#define __HTML_HH__

#include "url.h"               // for DilloUrl

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Keep in sync with the length of the Tags array. It is protected by an
 * static assert in html.cc to prevent errors) */
#define HTML_NTAGS 94

/*
 * Exported functions
 */
void a_Html_load_images(void *v_html, DilloUrl *pattern);
void a_Html_form_submit(void *v_html, void *v_form);
void a_Html_form_reset(void *v_html, void *v_form);
void a_Html_form_display_hiddens(void *v_html, void *v_form, bool_t display);

/*
 * Introspection and automation entry points used by the control socket
 * (src/IO/control.c). v_html is the DilloHtml* returned by
 * a_Bw_get_current_doc(). Indices are 0-based. Functions returning int use
 * 0 for success and small positive codes (documented per function) for
 * errors, so that control.c can map them onto the single-digit reply status.
 */

/* Links: html->links, same indices the renderer uses for link ids */
int a_Html_link_count(void *v_html);
/* NULL when out of range or when the entry itself is NULL */
DilloUrl *a_Html_link_url(void *v_html, int n);
/* One line per link: "N\tHREF\n" (HREF empty for NULL entries) */
void a_Html_links_describe(void *v_html, Dstr *out);

/* Forms: html->forms */
int a_Html_form_count(void *v_html);
/* NULL when out of range */
void *a_Html_get_form(void *v_html, int n);
/* One line per form: "N\tMETHOD\tACTION\tENCTYPE\n" */
void a_Html_forms_describe(void *v_html, Dstr *out);
/* One line per input of form n: "I\tTYPE\tNAME\tVALUE\n".
 * VALUE has backslash, tab and newline escaped as \\ \t \n.
 * Returns 0, or 1 if there is no form n. */
int a_Html_inputs_describe(void *v_html, int n, Dstr *out);
/* Set the value of the input called name in form n.
 * Returns 0 ok, 1 no such form, 2 no such input, 3 input type cannot be
 * set, 4 value not accepted (no such option/radio value, bad boolean). */
int a_Html_input_set(void *v_html, int n, const char *name, const char *value);
/* Submit form n as if the submit button called button was clicked (button
 * may be NULL: submit with no active button).
 * Returns 0 ok, 1 no such form, 2 no such submit button, 3 submission
 * refused (insecure-submission prompt cancelled or non-interactive mode),
 * 4 could not build the request (unknown method). */
int a_Html_form_submit_named(void *v_html, int n, const char *button);
/* Reset form n. Returns 0, or 1 if there is no form n. */
int a_Html_form_reset_n(void *v_html, int n);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __HTML_HH__ */
