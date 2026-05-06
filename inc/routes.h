/**
 * @file routes.h
 * @brief HTTP request dispatcher and REST API handler interface.
 *
 * Exposes a single Mongoose event-handler callback that routes incoming
 * HTTP requests to the appropriate handler function and falls back to
 * serving static files from the configured www directory.
 *
 * @author Subhajit Roy <subhajitroy005@gmail.com>
 * @date   2026-05-06
 */
#pragma once
#include "../mongoose/mongoose.h"

/**
 * @brief Main Mongoose event handler — register as the HTTP listen callback.
 *
 * Dispatches MG_EV_HTTP_MSG events to the correct REST endpoint handler
 * based on HTTP method and URI pattern.  Unmatched requests are served as
 * static files from the configured www directory.
 *
 * @param c       Active Mongoose connection.
 * @param ev      Mongoose event type.
 * @param ev_data Event-specific data (cast to mg_http_message* for HTTP).
 */
void http_handler(struct mg_connection *c, int ev, void *ev_data);
