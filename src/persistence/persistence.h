#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "../server.h"
#include <stdbool.h>

bool save_snapshot(db_t *db, const char *filepath);
bool load_snapshot(const char *filepath, db_t *db);

#endif // PERSISTENCE_H
