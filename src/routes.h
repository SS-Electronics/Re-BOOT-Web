#pragma once
#include "../mongoose/mongoose.h"

/* Main HTTP event handler — register as Mongoose callback */
void http_handler(struct mg_connection *c, int ev, void *ev_data);
