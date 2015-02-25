/*
	Ordo is program for calculating ratings of engine or chess players
    Copyright 2013 Miguel A. Ballicora

    This file is part of Ordo.

    Ordo is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ordo is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ordo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include "mytypes.h"
#include "plyrs.h"


bool_t
players_name2idx (const struct PLAYERS *plyrs, const char *player_name, player_t *pi)
{
	player_t j;
	bool_t found;
	for (j = 0, found = FALSE; !found && j < plyrs->n; j++) {
		found = !strcmp(plyrs->name[j], player_name);
		if (found) {
			*pi = j; 
		} 
	}
	return found;
}

