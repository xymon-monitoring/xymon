/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Bruno Manzoni */
/* configure: does rrd.h declare rrd_update() with "const char **"? */
#include <rrd.h>
int rrd_update(int, const char **);
int main(void) { return 0; }
