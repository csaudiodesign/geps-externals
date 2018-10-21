/*
 * GePS - Gesture-based Performance System
 * external implementing GePS Feedback Network
 * geps library class
 * 
 * Copyright (C) 2018  Cedric Spindler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * include the interface to Pd 
 */
#include "m_pd.h"
#include "geps.h"

static t_class *geps_class;

static void *geps_new(void)
{
  t_object *x = (t_object *)pd_new(geps_class);

  return (x);
}	

void fbnet_tilde_setup(void);

void geps_setup(void)
{
  geps_class = class_new(gensym("geps"), geps_new, 0,
    sizeof(t_object), CLASS_NOINLET, 0);

  /* Initialize objects contained in library */
  fbnet_tilde_setup();

  // /* Post library info to Pd window */
  post("GePS library loaded.");
  post("  built: "BUILD_DATE);
  post("  (c) Cedric Spindler");
  post("  cedric.spindler%cgmail.com", '@');
  post("  externals:");
  post("  - fbnet~");
}

