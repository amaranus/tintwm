#ifndef LAYOUTS_H
#define LAYOUTS_H

#include "tintwm.h"

enum layout { VERTICLE, HORIZONTAL };
extern enum layout layout;

void arrange(void);

#endif
