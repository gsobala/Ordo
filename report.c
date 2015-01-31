#include <stddef.h>
#include <assert.h>

#include "mytypes.h"
#include "report.h"
#include "encount.h"
#include "cegt.h"
#include "string.h"
#include "gauss.h"
#include "math.h"

//=== duplicated in main.c =========

static ptrdiff_t
head2head_idx_sdev (ptrdiff_t x, ptrdiff_t y)
{	
	ptrdiff_t idx;
	if (y < x) 
		idx = (x*x-x)/2+y;					
	else
		idx = (y*y-y)/2+x;
	return idx;
}

//==================

static void
calc_encounters__
				( int selectivity
				, const struct GAMES *g
				, const bool_t *flagged
				, struct ENCOUNTERS	*e
) 
{
	e->n = 
	calc_encounters	( selectivity
					, g
					, flagged
					, e->enc);

}

static struct PLAYERS 		Players;
static size_t N_relative_anchors = 100;
static int OUTDECIMALS = 1;
static double White_advantage = 0;
static double Drawrate_evenmatch = 0;
static double Confidence_factor = 1;

static struct relprior Raa[MAX_RELPRIORS];

#if 0
static int
compareit (const void *a, const void *b)
{
	const int *ja = (const int *) a;
	const int *jb = (const int *) b;

	const double da = RA.ratingof_results[*ja];
	const double db = RA.ratingof_results[*jb];
    
	return (da < db) - (da > db);
}
#else
static int
compareit (const void *a, const void *b)
{	
	//FIXME 
	return 1;
}
#endif

static size_t
find_maxlen (const char *nm[], size_t n)
{
	size_t maxl = 0;
	size_t length;
	size_t i;
	for (i = 0; i < n; i++) {
		length = strlen(nm[i]);
		if (length > maxl) maxl = length;
	}
	return maxl;
}

static bool_t 
is_super_player(size_t j)
{
	assert(Performance_type_set);
	return Players.performance_type[j] == PERF_SUPERLOSER || Players.performance_type[j] == PERF_SUPERWINNER;		
}

static const char *SP_symbolstr[3] = {"<",">"," "};

static const char *
get_super_player_symbolstr(size_t j)
{
	assert(Performance_type_set);
	if (Players.performance_type[j] == PERF_SUPERLOSER) {
		return SP_symbolstr[0];
	} else if (Players.performance_type[j] == PERF_SUPERWINNER) {
		return SP_symbolstr[1];
	} else
		return SP_symbolstr[2];
}

static bool_t
is_old_version(int32_t j)
{
	size_t i;
	bool_t found;
	for (i = 0, found = FALSE; !found && i < N_relative_anchors; i++) {
		found = j == Raa[i].player_b;
	}
	return found;
}

static double
rating_round(double x, int d)
{
	const int al[6] = {1,10,100,1000,10000,100000};
	int i;
	double y;
	if (d > 5) d = 5;
	if (d < 0) d = 0;
	y = x * al[d] + 0.5; 
	i = (int) floor(y);
	return (double)i/al[d];
}

static char *
get_sdev_str (double sdev, double confidence_factor, char *str)
{
	double x = sdev * confidence_factor;
	if (sdev > 0.00000001) {
		sprintf(str, "%6.*f", OUTDECIMALS, rating_round(x, OUTDECIMALS));
	} else {
		sprintf(str, "%s", "  ----");
	}
	return str;
}

//======================

void 
cegt_output	( const struct GAMES 	*g
			, const struct PLAYERS 	*p
			, const struct RATINGS 	*r
			, struct ENCOUNTERS 	*e  // memory just provided for local calculations
			, double 				*sdev
			, long 					simulate
			, double				confidence_factor
			, const struct GAMESTATS *pgame_stats
			, const struct DEVIATION_ACC *s)
{
	struct CEGT cegt;
	size_t j;

	calc_encounters__(ENCOUNTERS_NOFLAGGED, g, p->flagged, e);
	calc_obtained_playedby(e->enc, e->n, p->n, r->obtained, r->playedby);
	for (j = 0; j < p->n; j++) {
		r->sorted[j] = (int32_t) j; //FIXME size_t
	}
	qsort (r->sorted, (size_t)p->n, sizeof (r->sorted[0]), compareit);

	cegt.n_enc = e->n; 
	cegt.enc = e->enc;
	cegt.simulate = simulate;
	cegt.n_players = p->n;
	cegt.sorted = r->sorted;
	cegt.ratingof_results = r->ratingof_results;
	cegt.obtained_results = r->obtained_results;
	cegt.playedby_results = r->playedby_results;
	cegt.sdev = sdev; 
	cegt.flagged = p->flagged;
	cegt.name = p->name;
	cegt.confidence_factor = confidence_factor;

	cegt.gstat = pgame_stats;

	cegt.sim = s;

	output_cegt_style ("general.dat", "rating.dat", "programs.dat", &cegt);
}


// Function provided to have all head to head information

void 
head2head_output( const struct GAMES 	*g
				, const struct PLAYERS 	*p
				, const struct RATINGS 	*r
				, struct ENCOUNTERS 	*e  // memory just provided for local calculations
				, double 				*sdev
				, long 					simulate
				, double				confidence_factor
				, const struct GAMESTATS *pgame_stats
				, const struct DEVIATION_ACC *s
				, const char *head2head_str)
{
	struct CEGT cegt;
	size_t j;

	calc_encounters__(ENCOUNTERS_NOFLAGGED, g, p->flagged, e);
	calc_obtained_playedby(e->enc, e->n, p->n, r->obtained, r->playedby);
	for (j = 0; j < p->n; j++) {
		r->sorted[j] = (int32_t)j; //FIXME size_t
	}
	qsort (r->sorted, p->n, sizeof (r->sorted[0]), compareit);

	cegt.n_enc = e->n;
	cegt.enc = e->enc;
	cegt.simulate = simulate;
	cegt.n_players = p->n;
	cegt.sorted = r->sorted;
	cegt.ratingof_results = r->ratingof_results;
	cegt.obtained_results = r->obtained_results;
	cegt.playedby_results = r->playedby_results;
	cegt.sdev = sdev; 
	cegt.flagged = p->flagged;
	cegt.name = p->name;
	cegt.confidence_factor = confidence_factor;

	cegt.gstat = pgame_stats;

	cegt.sim = s;

	output_report_individual (head2head_str, &cegt, (int)simulate);
}


void
all_report 	( const struct GAMES 	*g
			, const struct PLAYERS 	*p
			, const struct RATINGS 	*r
			, struct ENCOUNTERS 	*e  // memory just provided for local calculations
			, double 				*sdev
			, long 					simulate
			, bool_t				hide_old_ver
			, double				confidence_factor
			, FILE 					*csvf
			, FILE 					*textf)
{
	FILE *f;
	size_t i, j;
	size_t ml;
	char sdev_str_buffer[80];
	const char *sdev_str;

	int rank = 0;
	bool_t showrank = TRUE;

	calc_encounters__(ENCOUNTERS_NOFLAGGED, g, p->flagged, e);

	calc_obtained_playedby(e->enc, e->n, p->n, r->obtained, r->playedby);

	for (j = 0; j < p->n; j++) {
		r->sorted[j] = (int32_t)j; //FIXME size_t
	}

	qsort (r->sorted, (size_t)p->n, sizeof (r->sorted[0]), compareit);

	/* output in text format */
	f = textf;
	if (f != NULL) {

		ml = find_maxlen (p->name, p->n);
		if (ml > 50) ml = 50;

		if (simulate < 2) {
			fprintf(f, "\n%s %-*s    :%7s %9s %7s %6s\n", 
				"   #", 			
				(int)ml,
				"PLAYER", "RATING", "POINTS", "PLAYED", "(%)");
	
			for (i = 0; i < p->n; i++) {

				j = (size_t)r->sorted[i]; //FIXME size_t
				if (!p->flagged[j]) {

					char rankbuf[80];
					showrank = !is_old_version((int32_t)j); //FIXME size_t
					if (showrank) {
						rank++;
						sprintf(rankbuf,"%d",rank);
					} else {
						rankbuf[0] = '\0';
					}

					if (showrank
						|| !hide_old_ver
					){
						fprintf(f, "%4s %-*s %s :%7.*f %9.1f %7d %6.1f%s\n", 
							rankbuf,
							(int)ml+1,
							p->name[j],
							get_super_player_symbolstr(j),
							OUTDECIMALS,
							rating_round (r->ratingof_results[j], OUTDECIMALS), 
							r->obtained_results[j], 
							r->playedby_results[j], 
							r->playedby_results[j]==0? 0: 100.0*r->obtained_results[j]/r->playedby_results[j], 
							"%"
						);
					}

				} else {

						fprintf(f, "%4lu %-*s   :%7s %9s %7s %6s%s\n", 
							i+1,
							(int)ml+1,
							p->name[j], 
							"----", "----", "----", "----","%");
				}
			}
		} else {
			fprintf(f, "\n%s %-*s    :%7s %6s %8s %7s %6s\n", 
				"   #", 
				(int)ml, 
				"PLAYER", "RATING", "ERROR", "POINTS", "PLAYED", "(%)");
	
			for (i = 0; i < p->n; i++) {
				j = (size_t) r->sorted[i]; //FIXME size_t

				sdev_str = get_sdev_str (sdev[j], confidence_factor, sdev_str_buffer);

				if (!p->flagged[j]) {

					char rankbuf[80];
					showrank = !is_old_version((int32_t)j);
					if (showrank) {
						rank++;
						sprintf(rankbuf,"%d",rank);
					} else {
						rankbuf[0] = '\0';
					}

					if (showrank
						|| !hide_old_ver
					){

						fprintf(f, "%4s %-*s %s :%7.*f %s %8.1f %7d %6.1f%s\n", 
						rankbuf,
						(int)ml+1, 
						p->name[j],
 						get_super_player_symbolstr(j),
						OUTDECIMALS,
						rating_round(r->ratingof_results[j], OUTDECIMALS), 
						sdev_str, 
						r->obtained_results[j], 
						r->playedby_results[j], 
						r->playedby_results[j]==0?0:100.0*r->obtained_results[j]/r->playedby_results[j], 
						"%"
						);
					}

				} else if (!is_super_player(j)) {
					fprintf(f, "%4lu %-*s   :%7.*f %s %8.1f %7d %6.1f%s\n", 
						i+1,
						(int)ml+1, 
						p->name[j], 
						OUTDECIMALS,
						rating_round(r->ratingof_results[j], OUTDECIMALS), 
						"  ****", 
						r->obtained_results[j], 
						r->playedby_results[j], 
						r->playedby_results[j]==0?0:100.0*r->obtained_results[j]/r->playedby_results[j], 
						"%"
					);
				} else {
					fprintf(f, "%4lu %-*s   :%7s %s %8s %7s %6s%s\n", 
						i+1,
						(int)ml+1,
						p->name[j], 
						"----", "  ----", "----", "----", "----","%"
					);
				}
			}
		}

		fprintf (f,"\n");
		fprintf (f,"White advantage = %.2f\n",White_advantage);
		fprintf (f,"Draw rate (equal opponents) = %.2f %s\n",100*Drawrate_evenmatch, "%");
		fprintf (f,"\n");

	} /*if*/

	/* output in a comma separated value file */
	f = csvf;
	if (f != NULL) {
			fprintf(f, "\"%s\""
			",%s"
			",%s"
			",%s"
			",%s"
			",%s"
			"\n"		
			,"Player"
			,"\"Rating\"" 
			,"\"Error\"" 
			,"\"Score\""
			,"\"Games\""
			,"\"(%)\"" 
			);
		for (i = 0; i < p->n; i++) {
			j = (size_t) r->sorted[i]; //FIXME size_t

				if (sdev[j] > 0.00000001) {
					sprintf(sdev_str_buffer, "%.1f", sdev[j] * confidence_factor);
					sdev_str = sdev_str_buffer;
				} else {
					sdev_str = "\"-\"";
				}

			fprintf(f, "\"%s\",%.1f"
			",%s"
			",%.2f"
			",%d"
			",%.2f"
			"\n"		
			,p->name[j]
			,r->ratingof_results[j] 
			,sdev_str
			,r->obtained_results[j]
			,r->playedby_results[j]
			,r->playedby_results[j]==0?0:100.0*r->obtained_results[j]/r->playedby_results[j] 
			);
		}
	}

	return;
}


void
errorsout(const struct PLAYERS *p, const struct RATINGS *r, const struct DEVIATION_ACC *s, const char *out)
{
	FILE *f;
	ptrdiff_t idx;
	int32_t y,x;
	size_t i, j;

	if (NULL != (f = fopen (out, "w"))) {

		fprintf(f, "\"N\",\"NAME\"");	
		for (i = 0; i < p->n; i++) {
			fprintf(f, ",%ld",i);		
		}
		fprintf(f, "\n");	

		for (i = 0; i < p->n; i++) {
			y = r->sorted[i];

			fprintf(f, "%ld,\"%21s\"", i, p->name[y]);

			for (j = 0; j < i; j++) {
				x = r->sorted[j];

				idx = head2head_idx_sdev ((ptrdiff_t)x, (ptrdiff_t)y);

				fprintf(f,",%.1f", s[idx].sdev * Confidence_factor);
			}

			fprintf(f, "\n");

		}

		fclose(f);

	} else {
		fprintf(stderr, "Errors with file: %s\n",out);	
	}
	return;
}


void
ctsout(const struct PLAYERS *p, const struct RATINGS *r, const struct DEVIATION_ACC *s, const char *out)
{
	FILE *f;
	ptrdiff_t idx;
	int32_t y,x;
	size_t i,j;

	if (NULL != (f = fopen (out, "w"))) {

		fprintf(f, "\"N\",\"NAME\"");	
		for (i = 0; i < p->n; i++) {
			fprintf(f, ",%ld",i);		
		}
		fprintf(f, "\n");	

		for (i = 0; i < p->n; i++) {
			y = r->sorted[i];
			fprintf(f, "%ld,\"%21s\"", i, p->name[y]);

			for (j = 0; j < p->n; j++) {
				double ctrs, sd, dr;
				x = r->sorted[j];
				if (x != y) {
					dr = r->ratingof_results[y] - r->ratingof_results[x];
					idx = head2head_idx_sdev ((ptrdiff_t)x, (ptrdiff_t)y);
					sd = s[idx].sdev;
					ctrs = 100*gauss_integral(dr/sd);
					fprintf(f,",%.1f", ctrs);
				} else {
					fprintf(f,",");
				}
			}
			fprintf(f, "\n");
		}
		fclose(f);
	} else {
		fprintf(stderr, "Errors with file: %s\n",out);	
	}
	return;
}

