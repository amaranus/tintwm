#ifndef LAYOUTS_H
#define LAYOUTS_H

#include "tintwm.h"

enum layout { VERTICLE, HORIZONTAL, FULLSCREEN_FLOAT };
extern enum layout layout;

void arrange(void);

#endif
