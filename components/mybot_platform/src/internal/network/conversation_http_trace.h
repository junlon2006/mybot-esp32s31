/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_CONVERSATION_HTTP_TRACE_H
#define MYBOT_CONVERSATION_HTTP_TRACE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct mybot_conversation_http_trace mybot_conversation_http_trace_t;

/* Optional diagnostics only. All functions accept NULL after allocation failure.
 * Feed only bytes actually accepted by TLS, never the attempted write length.
 * One trace belongs to one sequential HTTP connection. */
mybot_conversation_http_trace_t *mybot_conversation_http_trace_create(void);
void mybot_conversation_http_trace_send(mybot_conversation_http_trace_t *trace, const void *data,
                                        size_t size);
void mybot_conversation_http_trace_recv(mybot_conversation_http_trace_t *trace, const void *data,
                                        size_t size);
/* Logs only POST .../conversations/start bodies, then frees the trace.
 * eof means TLS returned 0; transport_error means a TLS operation failed. */
void mybot_conversation_http_trace_finish(mybot_conversation_http_trace_t *trace, bool eof,
                                          bool transport_error);

#endif
