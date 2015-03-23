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


#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "groups.h"
#include "mytypes.h"
#include "mymem.h"

#define NO_ID -1

//------------- Bit array begin --------------------------------------- 

typedef uint64_t pod_t;

struct BITARRAY {
	pod_t *		pod;
	player_t 	max;
};

typedef struct BITARRAY bitarray_t;

static void
ba_put(struct BITARRAY *ba, player_t x)
{
	if (x < ba->max) {
		ba->pod[x/64] |= (uint64_t)1 << (x & 63);
	}
}

static bool_t
ba_ison(struct BITARRAY *ba, player_t x)
{
	uint64_t y;
	bool_t ret;
	y = (uint64_t)1 & (ba->pod[x/64] >> (x & 63));	
	ret = y == 1;
	return ret;
}

static bool_t
ba_init(struct BITARRAY *ba, player_t max)
{
	bool_t ok;
	uint64_t *ptr;
	size_t i;
	size_t max_p = (size_t)max/64 + (max % 64 > 0?1:0);

	ok = NULL != (ptr = memnew (sizeof(pod_t)*max_p));
	if (ok) {
		ba->max = max;
		ba->pod = ptr;
		for (i = 0; i < max_p; i++) ba->pod[i] = 0;
	} 
	return ok;
}

static void
ba_done(struct BITARRAY *ba)
{
	assert(ba->pod);
	if (ba->pod) memrel (ba->pod);
	ba->pod = NULL;
	ba->max = 0;
}

//---------------------------------------------------------------------

static const char 		**Namelist = NULL;

static struct GROUP_BUFFER 			group_buffer;
static struct PARTICIPANT_BUFFER	participant_buffer;
static struct CONNECT_BUFFER		connection_buffer;

static bool_t	group_buffer_init(struct GROUP_BUFFER *g, player_t n);
static void		group_buffer_done(struct GROUP_BUFFER *g);
static bool_t	participant_buffer_init(struct PARTICIPANT_BUFFER *x, player_t n);
static void		participant_buffer_done(struct PARTICIPANT_BUFFER *x);
static bool_t	connection_buffer_init(struct CONNECT_BUFFER *x, gamesnum_t n);
static void		connection_buffer_done(struct CONNECT_BUFFER *x);

//---------------------------------------------------------------------

// supporting memory

	// enc

	// SE:  list of "Super" encounters
	// SE2: list of "Super" encounters that do not belong to the same group

static struct ENC *	SE   = NULL;
static struct ENC *	SE2  = NULL;
static gamesnum_t	N_se  = 0;
static gamesnum_t	N_se2 = 0;

	// groups

static player_t		N_players = 0;

static player_t	*	Group_belong;
static player_t		N_groups;

static bitarray_t 	BA;
static bitarray_t	BB;
static player_t *	Get_new_id;

static group_t 	**	Group_final_list;
static player_t		Group_final_list_n = 0;

static node_t	*	Gnode;

static player_t	*	CHAIN;

//----------------------------------------------------------------------

static void				simplify_all (void);
static void				finish_it (void);
static void 			connect_init (void) {connection_buffer.n = 0;}
static connection_t *	connection_new (void) 
{
	assert (connection_buffer.n < connection_buffer.max);
	return &connection_buffer.list[connection_buffer.n++];
}

static void 			participant_init (void) {participant_buffer.n = 0;}
static participant_t *	participant_new (void) 
{
	assert (participant_buffer.n < participant_buffer.max);	
	return &participant_buffer.list[participant_buffer.n++];
}

// prototypes
static group_t * 	group_new (void);
static group_t * 	group_reset (group_t *g);
static group_t * 	group_combined (group_t *g);
static group_t * 	group_pointed_by_conn (connection_t *c);
static group_t *	group_pointed_by_node (node_t *nd);
static void			final_list_output (FILE *f);
static void			group_output (FILE *f, group_t *s);

// groupset functions

#ifndef NDEBUG
static bool_t
groupset_sanity_check(void)
{ 
	group_t *c; 

	c = group_buffer.prehead;
	if (c != NULL)
		c = c->next;
	else
		return FALSE;

	for (; c != NULL; c = c->next) {
		if (c->prev == NULL) return FALSE;
	}
	return TRUE;
}
#endif

static void groupset_init (void) 
{
	group_buffer.tail    = &group_buffer.list[0];
	group_buffer.prehead = &group_buffer.list[0];
	group_reset(group_buffer.prehead);
	group_buffer.n = 1;
}

static group_t * groupset_tail (void) {return group_buffer.tail;}
static group_t * groupset_head (void) {return group_buffer.prehead->next;}

#if 0
// for debuggin purposes
static void groupset_print(void)
{
	group_t * s;
	printf("ID added {\n");
	for (s = groupset_head(); s != NULL; s = s->next) {
		printf("  id=%d\n",s->id);
	}
	printf("}\n");
	return;
}
#endif

static void groupset_add (group_t *a) 
{
	group_t *t = groupset_tail();
	t->next = a;
	a->prev = t;
	group_buffer.tail = a;
}

static group_t * groupset_find (player_t id)
{
	group_t * s;
	for (s = groupset_head(); s != NULL; s = s->next) {
		if (id == s->id) return s;
	}
	return NULL;
}

//===

static group_t * group_new (void) 
{
	return &group_buffer.list[group_buffer.n++];
}

static group_t * group_reset (group_t *g)
{		
	if (g == NULL) return NULL;
	g->next = NULL;	
	g->prev = NULL; 
	g->combined = NULL;
	g->pstart = NULL; g->plast = NULL; 	
	g->cstart = NULL; g->clast = NULL;
	g->lstart = NULL; g->llast = NULL;
	g->id = NO_ID;
	g->isolated = FALSE;
	return g;
}

static group_t * group_combined (group_t *g)
{
	while (g->combined != NULL)
		g = g->combined;
	return g;
}

static void
add_participant (group_t *g, player_t i)
{
	participant_t *nw;
	nw = participant_new();
	nw->next = NULL;
	nw->name = Namelist[i];
	nw->id = i; 

	if (g->pstart == NULL) {
		g->pstart = nw; 
		g->plast = nw;	
	} else {
		g->plast->next = nw;	
		g->plast = nw;
	}
}

static void
add_connection (group_t *g, player_t i)
{
	player_t group_id;
	connection_t *nw = connection_new();
	nw->next = NULL;
	nw->node = &Gnode[i];

	group_id = NO_ID;
	if (Gnode[i].group) group_id = Gnode[i].group->id;

	if (g->cstart == NULL) {
		g->cstart = nw; 
		g->clast = nw;	
	} else {
		connection_t *l, *c;
		bool_t found = FALSE;
		for (c = g->cstart; !found && c != NULL; c = c->next) {
			node_t *nd = c->node;
			found = nd && nd->group && nd->group->id == group_id;
		}
		if (!found) {
			l = g->clast;
			l->next  = nw;
			g->clast = nw;
		}
	}		
}

static void
add_revconn (group_t *g, player_t i)
{
	player_t group_id;
	connection_t *nw = connection_new();
	nw->next = NULL;
	nw->node = &Gnode[i];

	group_id = NO_ID;
	if (Gnode[i].group) group_id = Gnode[i].group->id;

	if (g->lstart == NULL) {
		g->lstart = nw; 
		g->llast = nw;	
	} else {
		connection_t *l, *c;
		bool_t found = FALSE;
		for (c = g->lstart; !found && c != NULL; c = c->next) {
			node_t *nd = c->node;
			found = nd && nd->group && nd->group->id == group_id;
		}
		if (!found) {
			l = g->llast;
			l->next  = nw;
			g->llast = nw;
		}
	}		
}

static player_t get_iwin (struct ENC *pe) {return pe->wscore > 0.5? pe->wh: pe->bl;}
static player_t get_ilos (struct ENC *pe) {return pe->wscore > 0.5? pe->bl: pe->wh;}

static void
enc2groups (struct ENC *pe)
{
	player_t iwin, ilos;
	group_t *glw, *gll, *g;

	assert(pe);

	iwin = get_iwin(pe);
	ilos = get_ilos(pe);

	if (Gnode[iwin].group == NULL) {

		g = groupset_find (Group_belong[iwin]);
		if (g == NULL) {
			// creation
			g = group_reset(group_new());	
			g->id = Group_belong[iwin];
			groupset_add(g);
			Gnode[iwin].group = g;
		}
		glw = g;
	} else {
		glw = Gnode[iwin].group;
	}

	if (Gnode[ilos].group == NULL) {

		g = groupset_find (Group_belong[ilos]);
		if (g == NULL) {
			// creation
			g = group_reset(group_new());	
			g->id = Group_belong[ilos];
			groupset_add(g);
			Gnode[ilos].group = g;
		}
		gll = g;
	} else {
		gll = Gnode[ilos].group;
	}

	Gnode[iwin].group = glw;
	Gnode[ilos].group = gll;
	add_connection (glw, ilos);
	add_revconn (gll, iwin);
}

static void
group_gocombine (group_t *g, group_t *h);

static void
ifisolated2group (player_t x)
{
	group_t *g;

	if (Gnode[x].group == NULL) {
		g = groupset_find (Group_belong[x]);
		if (g == NULL) {
			// creation
			g = group_reset(group_new());	
			g->id = Group_belong[x];
			groupset_add(g);
			Gnode[x].group = g;
		}
		Gnode[x].group = g;
	} 
}

static void
convert_general_init (player_t n_plyrs)
{
	player_t i;
	connect_init();
	participant_init();
	groupset_init();
	for (i = 0; i < n_plyrs; i++) {
		Gnode[i].group = NULL;
	}
	return;
}

static player_t
groups_counter (void)
{
	return Group_final_list_n;
}

player_t
convert_to_groups (FILE *f, player_t n_plyrs, const char **name)
{
	player_t i;
	gamesnum_t e;

	Namelist = name;

	convert_general_init (n_plyrs);

	for (e = 0 ; e < N_se2; e++) {
		enc2groups(&SE2[e]);
	}
	for (i = 0; i < n_plyrs; i++) {
		ifisolated2group(i);
	}

	for (i = 0; i < n_plyrs; i++) {
		player_t gb; 
		group_t *g;
		gb = Group_belong[i];
		g = groupset_find(gb);
		assert(g);
		add_participant(g, i);	
	}

	simplify_all();
	finish_it();
	if (NULL != f) {
		if (groups_counter() > 1) {
			final_list_output(f);
		} else {
			assert (1 == groups_counter());
			fprintf (f,"All players are connected into only one group.\n");
		}	
	}
	return groups_counter() ;
}

static player_t
group_belonging (player_t plyr)
{
	group_t *g = group_pointed_by_node (&Gnode[plyr]);
	if (g) 
		return g->id;
	else
		return NO_ID;
}

void
sieve_encounters	( const struct ENC *enc
					, gamesnum_t n_enc
					, struct ENC *enca
					, gamesnum_t *N_enca
					, struct ENC *encb
					, gamesnum_t *N_encb
)
{
	gamesnum_t e;
	player_t w,b;
	gamesnum_t na = 0, nb = 0;

	*N_enca = 0;
	*N_encb = 0;

	for (e = 0; e < n_enc; e++) {
		w = enc[e].wh; 
		b = enc[e].bl; 
		if (group_belonging(w) == group_belonging(b)) {
			enca[na] = enc[e]; na += 1;
		} else {
			encb[nb] = enc[e]; nb += 1;
		}
	} 

	*N_enca = na;
	*N_encb = nb;

	return;
}

static void
group_gocombine (group_t *g, group_t *h)
{
	// unlink h
	group_t *pr = h->prev;
	group_t *ne = h->next;

	if (h->combined == g) {
		return;
	}	

	h->prev = NULL;
	h->next = NULL;

	assert(pr);
	pr->next = ne;

	if (ne) ne->prev = pr;
	
	h->combined = g;
	
	g->plast->next = h->pstart;
	g->plast = h->plast;
	h->plast = NULL;
	h->pstart = NULL;	

	g->clast->next = h->cstart;
	g->clast = h->clast;
	h->clast = NULL;
	h->cstart = NULL;

	g->llast->next = h->lstart;
	g->llast = h->llast;
	h->llast = NULL;
	h->lstart = NULL;
}

//-----------------------------------------------

static group_t *
group_pointed_by_conn (connection_t *c)
{
	node_t *nd; 
	if (c == NULL) return NULL;
	nd = c->node;
	if (nd) {
		group_t *gr = nd->group;
		if (gr) {
			gr = group_combined(gr);
		} 
		return gr;
	} else {
		return NULL;
	}
}

static group_t *
group_pointed_by_node (node_t *nd)
{
	if (nd) {
		group_t *gr = nd->group;
		if (gr) {
			gr = group_combined(gr);
		} 
		return gr;
	} else {
		return NULL;
	}
}

static void simplify (group_t *g);

static group_t *group_next (group_t *g)
{
	assert(g);
	return g->next;
}

static void
simplify_all (void)
{
	group_t *g;

	g = groupset_head();
	assert(g);
	while(g) {
		simplify(g);
		g = group_next(g);
	}
	return;
}

#if 0
static void
beat_lost_output (group_t *g)
{
	group_t 		*beat_to, *lost_to;
	connection_t 	*c;

	printf ("G=%d, beat_to: ",g->id);

		// loop connections, examine id if repeated or self point (delete them)
		beat_to = NULL;
		c = g->cstart; 
		do {
			if (c && NULL != (beat_to = group_pointed_by_conn(c))) {
				printf ("%d, ",beat_to->id);
				c = c->next;
			}
		} while (c && beat_to);

	printf ("\n");
	printf ("G=%d, lost_to: ",g->id);

		lost_to = NULL;
		c = g->lstart; 
		do {
			if (c && NULL != (lost_to = group_pointed_by_conn(c))) {
				printf ("%d, ",lost_to->id);
				c = c->next;
			}
		} while (c && lost_to);
	printf ("\n");
}
#endif

static void
simplify_shrink__ (group_t *g)
{
	group_t 		*beat_to, *lost_to;
	connection_t 	*c, *p;
	player_t		id, oid;

	id = NO_ID;

	ba_init(&BA, N_players); // it was -1
	ba_init(&BB, N_players); // it was -2

	oid = g->id; // own id

	// loop connections, examine id if repeated or self point (delete them)
	beat_to = NULL;
	do {
		c = g->cstart; 
		if (c && NULL != (beat_to = group_pointed_by_conn(c))) {
			id = beat_to->id;
			if (id == oid) { 
				// remove connection
				g->cstart = c->next; //FIXME mem leak? memrel(c)
			}
		}
	} while (c && beat_to && id == oid);


	if (c && beat_to) {

		ba_put(&BA, id);
		p = c;
		c = c->next;

		while (c != NULL) {
			beat_to = group_pointed_by_conn(c);
			id = beat_to->id;
			if (id == oid || ba_ison(&BA, id)) {
				// remove connection and advance
				c = c->next; //FIXME mem leak? memrel(c)
				p->next = c; 
			}
			else {
				// remember and advance
				ba_put(&BA, id);
				p = c;
				c = c->next;
			}
		}

	}

	// loop connections, examine id if repeated or self point (delete them)

	lost_to = NULL;

	do {
		c = g->lstart; 
		if (c && NULL != (lost_to = group_pointed_by_conn(c))) {
			id = lost_to->id;
			if (id == oid) { 
				// remove connection
				g->lstart = c->next; //FIXME mem leak?
			}
		}
	} while (c && lost_to && id == oid);


	if (c && lost_to) {

		ba_put(&BB, id);
		p = c;
		c = c->next;

		while (c != NULL) {
			lost_to = group_pointed_by_conn(c);
			id = lost_to->id;
			if (id == oid || ba_ison(&BB, id)) {
				// remove connection and advance
				c = c->next;		
				p->next = c; //FIXME mem leak?
			}
			else {
				// remember and advance
				ba_put(&BB, id);
				p = c;
				c = c->next;
			}
		}
	}

	ba_done(&BA);
	ba_done(&BB);

	return;
}

static void
simplify_shrink (group_t *g)
{
	//printf("-------------\n");
	//printf("before shrink\n");
	//beat_lost_output (g);
	simplify_shrink__ (g);
	//printf("after  shrink\n");
	//beat_lost_output (g);
	//printf("-------------\n");
}

static void
simplify (group_t *g)
{
	group_t 		*beat_to, *lost_to, *combine_with=NULL;
	connection_t 	*c, *p;
	player_t		id, oid;
	bool_t 			gotta_combine = FALSE;
	bool_t			combined = FALSE;

	id=NO_ID;

	do {
		simplify_shrink (g);

		assert(groupset_sanity_check());

		ba_init(&BA, N_players); // was - 1
		ba_init(&BB, N_players); // was - 2 

		oid = g->id; // own id

		gotta_combine = FALSE;

		// loop connections, examine id if repeated or self point (delete them)
		beat_to = NULL;
		do {
			c = g->cstart; 
			if (c && NULL != (beat_to = group_pointed_by_conn(c))) {
				id = beat_to->id;
				if (id == oid) { 
					// remove connection
					g->cstart = c->next; //FIXME mem leak? memrel(c)
				}
			}
		} while (c && beat_to && id == oid);


		if (c && beat_to) {

			ba_put(&BA, id);
			p = c;
			c = c->next;

			while (c != NULL) {
				beat_to = group_pointed_by_conn(c);
				id = beat_to->id;
				if (id == oid || ba_ison(&BA, id)) {
					// remove connection and advance
					c = c->next; //FIXME mem leak? memrel(c)
					p->next = c; 
				}
				else {
					// remember and advance
					ba_put(&BA, id);
					p = c;
					c = c->next;
				}
			}

		}

		//=====
		// loop connections, examine id if repeated or self point (delete them)

		lost_to = NULL;

		assert(groupset_sanity_check());

		do {
			c = g->lstart; 
			if (c && NULL != (lost_to = group_pointed_by_conn(c))) {
				id = lost_to->id;
				if (id == oid) { 
					// remove connection
					g->lstart = c->next; //FIXME mem leak?
				}
			}
		} while (c && lost_to && id == oid);

		//if (lost_to) printf ("found=%d\n",lost_to->id); else printf("no lost to found\n");

		assert(groupset_sanity_check());

		if (c && lost_to) {

			// GOTTACOMBINE?
			if (ba_ison(&BA, id)) {

				assert(groupset_sanity_check());

				gotta_combine = TRUE;
				combine_with = lost_to;
			}
			else gotta_combine = FALSE;

			ba_put(&BB, id);
			p = c;
			c = c->next;

			assert(groupset_sanity_check());

			while (c != NULL && !gotta_combine) {
				lost_to = group_pointed_by_conn(c);
				id = lost_to->id;
				if (id == oid || ba_ison(&BB, id)) {
					// remove connection and advance
					c = c->next;		
					p->next = c; //FIXME mem leak?
				}
				else {
					// remember and advance
					ba_put(&BB, id);
					p = c;
					c = c->next;
	
					// GOTTACOMBINE?
					if (ba_ison(&BA, id)) {
						gotta_combine = TRUE;
						combine_with = lost_to;
					}
					else gotta_combine = FALSE;
				}
			}

			assert(groupset_sanity_check());
		}

		ba_done(&BA);
		ba_done(&BB);

		if (gotta_combine) {
			//printf("combine g=%d combine_with=%d\n",g->id, combine_with->id);
			assert(groupset_sanity_check());

			group_gocombine(g,combine_with);

			combined = TRUE;
		} else {
			combined = FALSE;
		}
	
	} while (combined);

	//printf("----final----\n");
	simplify_shrink (g);

	return;
}

//======================

static connection_t *group_beathead (group_t *g) {return g->cstart;} 
static connection_t *beat_next (connection_t *b) {return b->next;} 

static group_t *
group_unlink (group_t *g)
{
	group_t *a, *b; 
	assert(g);
	a = g->prev;
	b = g->next;
	if (a) a->next = b;
	if (b) b->prev = a;
	g->prev = NULL;
	g->next = NULL;
	g->isolated = TRUE;
	return g;
}

static group_t *
group_next_pointed_by_beat (group_t *g)
{
	group_t *gp;
	connection_t *b;
	player_t own_id;
	if (g == NULL)	return NULL; 
	own_id = g->id;
	b = group_beathead(g);
	if (b == NULL)	return NULL; 

	gp = group_pointed_by_conn(b);
	while (gp == NULL || gp->isolated || gp->id == own_id) {
		b = beat_next(b);
		if (b == NULL) return NULL;
		gp = group_pointed_by_conn(b);
	} 

	return gp;
}

static void
finish_it (void)
{
	player_t *chain_end;
	group_t *g, *h, *gp;
	connection_t *b;
	player_t own_id, bi;
	player_t *chain;
	bool_t startover;
	bool_t combined;

	do {
		startover = FALSE;
		combined = FALSE;

		chain = CHAIN;

		ba_init(&BA, N_players);

		g = groupset_head();
		if (g == NULL) break;

		own_id = g->id; // own id

		do {
			ba_put(&BA, own_id);
			*chain++ = own_id;

			h = group_next_pointed_by_beat(g);

			if (h == NULL) {

				Group_final_list[Group_final_list_n++] = group_unlink(g);

				ba_done(&BA);
				startover = TRUE;

			} else {
			
				g = h;

				own_id = g->id;

				for (b = group_beathead(g); b != NULL; b = beat_next(b)) {

					gp = group_pointed_by_conn(b);
					bi = gp->id;
	
					if (ba_ison(&BA, bi)) {
						//	findprevious bi, combine... remember to include own id;
						player_t *p;
						chain_end = chain;
						while (chain-->CHAIN) {
							if (*chain == bi) break;
						}
						
						for (p = chain; p < chain_end; p++) {
							group_t *x, *y;
							//printf("combine x=%d y=%d\n",own_id, *p);
							x = group_pointed_by_node(Gnode + own_id);
							y = group_pointed_by_node(Gnode + *p);
							group_gocombine(x,y);
							combined = TRUE;
							startover = TRUE;
							break;
						}
						break;
					}
				}	
			}
		} while (!combined && !startover);
	} while (startover);

	return;
}


static void
final_list_output (FILE *f)
{
	group_t *g;
	player_t i;
	long new_id;

	for (i = 0; i < N_players; i++) {
		Get_new_id[i] = NO_ID;
	}

	new_id = 0;
	for (i = 0; i < Group_final_list_n; i++) {
		g = Group_final_list[i];
		new_id++;
		Get_new_id[g->id] = new_id;
	}

	for (i = 0; i < Group_final_list_n; i++) {
		g = Group_final_list[i];
		fprintf (f,"\nGroup %ld\n",(long)Get_new_id[g->id]);

		//printf("-post-final--\n");
		simplify_shrink (g);

		group_output(f,g);
	}

	fprintf(f,"\n");
}

static int compare_str (const void * a, const void * b)
{
	const char * const *ap = a;
	const char * const *bp = b;
	return strcmp(*ap,*bp);
}

static size_t
participants_list_population (participant_t *pstart)
{
	size_t group_n;
	participant_t *p;

	for (p = pstart, group_n = 0; p != NULL; p = p->next) {
		group_n++;
	}
	return group_n;
}

static size_t
group_population (group_t *s)
{		
	return	participants_list_population (s->pstart);
}

static size_t
final_list_population_min (void)
{
	group_t *g;
	int i;
	size_t x, min = 0;

	for (i = 0; i < Group_final_list_n; i++) {
		g = Group_final_list[i];
		simplify_shrink (g);
		x = group_population(g);
		if (i == 0) {
			min = x;
		} else {
			min = x < min? x: min;
		}
	}
	assert(min != 0);
	return min;
}


static void
participants_list_print (FILE *f, participant_t *pstart)
{
	size_t group_n, n, i;
	const char **arr;
	participant_t *p;

	group_n = participants_list_population (pstart); // how many?

	if (NULL != (arr = memnew (sizeof(char *) * group_n))) {
		
		for (p = pstart, n = 0; p != NULL; p = p->next, n++) {
			arr[n] = p->name;
		}

		qsort (arr, group_n, sizeof(char *), compare_str);

		for (i = 0; i < group_n; i++) {
			fprintf (f," | %s\n",arr[i]);
		}

		memrel(arr);
	} else {
		// catch error, not enough memory, so print unordered
		for (p = pstart; p != NULL; p = p->next) {
			fprintf (f," | %s\n",p->name);
		}
	}
}

static void
group_output (FILE *f, group_t *s)
{		
	connection_t *c;
	player_t own_id;
	int winconnections = 0, lossconnections = 0;
	assert(s);
	own_id = s->id;

	participants_list_print (f, s->pstart);

	for (c = s->cstart; c != NULL; c = c->next) {
		group_t *gr = group_pointed_by_conn(c);
		if (gr != NULL) {
			if (gr->id != own_id) {
				fprintf (f," \\---> there are (only) wins against group: %ld\n",(long)Get_new_id[gr->id]);
				winconnections++;
			}
		} else
			fprintf (f,"point to node NULL\n");
	}
	for (c = s->lstart; c != NULL; c = c->next) {
		group_t *gr = group_pointed_by_conn(c);
		if (gr != NULL) {
			if (gr->id != own_id) {
				fprintf (f," \\---> there are (only) losses against group: %ld\n",(long)Get_new_id[gr->id]);
				lossconnections++;
			}
		} else
			fprintf (f,"pointed by node NULL\n");
	}
	if (winconnections == 0 && lossconnections == 0) {
		fprintf (f," \\---> this group is isolated from the rest\n");
	} 
}

static bool_t encounter_is_SW (const struct ENC *e) 
{
	return e->W > 0 && e->D == 0 && e->L == 0;
}

static bool_t encounter_is_SL (const struct ENC *e) 
{
	return e->W == 0 && e->D == 0 && e->L > 0;
}

void
scan_encounters (const struct ENC *enc, gamesnum_t n_enc, player_t n_plyrs)
{
/*
	static variables modified:
		N_groups
		Groups_belong
		SE
		N_se
		SE2
		N_se2
*/
	player_t i;
	gamesnum_t e;
	const struct ENC *pe;
	player_t gw, gb, lowerg, higherg;

	assert (SE != NULL);
	assert (SE2!= NULL);
	assert (N_se  == 0);
	assert (N_se2 == 0);

	N_se  = 0;
	N_se2 = 0;

	N_groups = n_plyrs;
	for (i = 0; i < n_plyrs; i++) {
		Group_belong[i] = (int32_t)i;
	}

	for (e = 0; e < n_enc; e++) {

		pe = &enc[e];
		if (encounter_is_SL(pe) || encounter_is_SW(pe)) {
			SE[N_se++] = *pe;
		} else {
			gw = Group_belong[pe->wh];
			gb = Group_belong[pe->bl];
			if (gw != gb) {
				lowerg   = gw < gb? gw : gb;
				higherg  = gw > gb? gw : gb;
				// join
				for (i = 0; i < n_plyrs; i++) {
					if (Group_belong[i] == higherg) {
						Group_belong[i] = lowerg;
					}
				}
				N_groups--;
			}
		}
	} 

	for (e = 0, N_se2 = 0 ; e < N_se; e++) {
		player_t x,y;
		x = SE[e].wh;
		y = SE[e].bl;	
		if (Group_belong[x] != Group_belong[y]) {
			SE2[N_se2++] = SE[e];
		}
	}

	// SE:  list of "Super" encounters
	// SE2: list of "Super" encounters that do not belong to the same group

	return;
}


bool_t
supporting_encmem_init (gamesnum_t nenc)
{
	struct ENC *a;
	struct ENC *b;

	if (NULL == (a = memnew (sizeof(struct ENC) * (size_t)nenc))) {
		return FALSE;
	} else 
	if (NULL == (b = memnew (sizeof(struct ENC) * (size_t)nenc))) {
		memrel(a);
		return FALSE;
	}

	SE  = a;
	SE2 = b;

	return TRUE;
}

void
supporting_encmem_done (void)
{
	if (SE ) memrel (SE );
	if (SE2) memrel (SE2);
	N_se  = 0;
	N_se2 = 0;
	return;
}

bool_t
supporting_groupmem_init (player_t nplayers, gamesnum_t nenc)
{
	player_t 	*a;
	player_t	*b;
	group_t *	*c;
	node_t 		*d;
	player_t	*e;

	size_t		sa = sizeof(player_t);
	size_t		sb = sizeof(player_t);
	size_t		sc = sizeof(group_t *);
	size_t		sd = sizeof(node_t);
	size_t		se = sizeof(player_t);

	if (NULL == (a = memnew (sa * (size_t)nplayers))) {
		return FALSE;
	} else 
	if (NULL == (b = memnew (sb * (size_t)nplayers))) {
		memrel(a);
		return FALSE;
	} else 
	if (NULL == (c = memnew (sc * (size_t)nplayers))) {
		memrel(a);
		memrel(b);
		return FALSE;
	} else 
	if (NULL == (d = memnew (sd * (size_t)nplayers))) {
		memrel(a);
		memrel(b);
		memrel(c);
		return FALSE;
	} else 
	if (NULL == (e = memnew (se * (size_t)nplayers))) {
		memrel(a);
		memrel(b);
		memrel(c);
		memrel(d);
		return FALSE;
	}

	Group_belong = a;
	Get_new_id = b;
	Group_final_list = c;
	Gnode = d;
	CHAIN = e;

	N_players = nplayers;

	if (!group_buffer_init (&group_buffer, nplayers)) {
		return FALSE;
	}
	if (!participant_buffer_init (&participant_buffer, nplayers)) {
		 group_buffer_done (&group_buffer);
		return FALSE;
	}
	if (!connection_buffer_init (&connection_buffer, nenc*2)) {
		 group_buffer_done (&group_buffer);
		 participant_buffer_done (&participant_buffer);
		return FALSE;
	}

	return TRUE;
}

void
supporting_groupmem_done (void)
{
	if (Group_belong)	 	memrel (Group_belong );
	if (Get_new_id) 		memrel (Get_new_id);
	if (Group_final_list) 	memrel (Group_final_list);
	if (Gnode) 				memrel (Gnode);
	if (CHAIN) 				memrel (CHAIN);

	Group_belong = NULL;
	Get_new_id = NULL;
	Group_final_list = NULL;
	Gnode = NULL;
	CHAIN = NULL;

	N_groups = 0;
	Group_final_list_n = 0;
	N_players = 0;

	group_buffer_done (&group_buffer);
	participant_buffer_done (&participant_buffer);
	connection_buffer_done (&connection_buffer);

	return;
}

//==

static bool_t
group_buffer_init (struct GROUP_BUFFER *g, player_t n)
{
	group_t *p;
	size_t elements = (size_t)n + 1; //one extra added for the head
	if (NULL != (p = memnew (sizeof(group_t) * elements))) {
		g->list = p;		
		g->tail = NULL;
		g->prehead = NULL;
		g->n = 0;
		g->max = n;
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
group_buffer_done (struct GROUP_BUFFER *g)
{
	if (g->list != NULL) {
		memrel(g->list);
		g->list = NULL;
	}
	g->tail = NULL;
	g->prehead = NULL;
	g->n = 0;
}


static bool_t
participant_buffer_init (struct PARTICIPANT_BUFFER *x, player_t n)
{
	participant_t *p;
	if (NULL != (p = memnew (sizeof(participant_t) * (size_t)n))) {
		x->list = p;		
		x->n = 0;
		x->max = n;
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
participant_buffer_done (struct PARTICIPANT_BUFFER *x)
{
	if (x->list != NULL) {
		memrel(x->list);
		x->list = NULL;
	}
	x->n = 0;
}

static bool_t
connection_buffer_init (struct CONNECT_BUFFER *x, gamesnum_t n)
{
	connection_t *p;
	if (NULL != (p = memnew (sizeof(connection_t) * (size_t)n))) {
		x->list = p;		
		x->n = 0;
		x->max = n;
		return TRUE;
	} else {
		return FALSE;
	}
}

static void
connection_buffer_done (struct CONNECT_BUFFER *x)
{
	if (x->list != NULL) {
		memrel(x->list);
		x->list = NULL;
	}
	x->n = 0;
}


bool_t
groups_process	( bool_t quiet
				, const struct ENCOUNTERS *encounters
				, const struct PLAYERS *players
				, FILE *groupf) 
{
	bool_t ok = FALSE;

	if (supporting_encmem_init (encounters->n)) {
		struct ENC *	a;
		struct ENC *	b;
		struct ENC *	Encounter2;
		struct ENC *	Encounter3;
		gamesnum_t		N_encounters2 = 0;
		gamesnum_t 		N_encounters3 = 0;
		gamesnum_t 		nenc = encounters->n;

		assert (nenc > 0);
		if (NULL == (a = memnew (sizeof(struct ENC) * (size_t)nenc))) {
			ok = FALSE;
		} else 
		if (NULL == (b = memnew (sizeof(struct ENC) * (size_t)nenc))) {
			ok = FALSE;
			memrel(a);
		} else {
			Encounter2 = a;
			Encounter3 = b;

			if (supporting_groupmem_init (players->n, encounters->n)) {
				player_t groups_n;
				ok = TRUE;
				scan_encounters (encounters->enc, encounters->n, players->n); 
				groups_n = convert_to_groups (groupf, players->n, players->name);
				sieve_encounters (encounters->enc, encounters->n, Encounter2, &N_encounters2, Encounter3, &N_encounters3);
				if (!quiet) {
					printf ("Groups=%ld\n", (long)groups_n);
					printf ("Encounters, Total=%ld, Main=%ld, @ Interface between groups=%ld\n"
								,(long)encounters->n, (long)N_encounters2, (long)N_encounters3);
				}
				supporting_groupmem_done ();
			} else {
				ok = FALSE;
			}
			memrel(a);
			memrel(b);
		}
		supporting_encmem_done ();
	} 
	return ok;
}


bool_t
groups_process_to_count (const struct ENCOUNTERS *encounters, const struct PLAYERS *players, player_t *n) 
{
	bool_t ok = FALSE;
	if (supporting_encmem_init (encounters->n)) {
		if (supporting_groupmem_init (players->n, encounters->n)) {
			ok = TRUE;
			scan_encounters(encounters->enc, encounters->n, players->n); 
			*n = convert_to_groups(NULL, players->n, players->name);
			supporting_groupmem_done ();
		} else {
			ok = FALSE;
		}
		supporting_encmem_done ();
	} 
	return ok;
}

bool_t
group_is_problematic(const struct ENCOUNTERS *encounters, const struct PLAYERS *players)
{
	player_t n;
	bool_t ok = FALSE;
	if (supporting_encmem_init (encounters->n)) {
		if (supporting_groupmem_init (players->n, encounters->n)) {
			scan_encounters(encounters->enc, encounters->n, players->n); 
			n = convert_to_groups(NULL, players->n, players->name);
			if (n == 1) {
				ok = TRUE;
			} else if (n == 2) {
				ok = 1 == final_list_population_min();
			} else {
				ok = FALSE;
			}
			supporting_groupmem_done ();
		} else {
			ok = FALSE;
		}
		supporting_encmem_done ();
	} 
	return !ok;
}
