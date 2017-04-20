#ifndef ELOG_H
#define ELOG_H

#include "nstr.h"

void elog_err(const char *format, ...);
void elog_cmd(nstr_t cmd);

#endif
