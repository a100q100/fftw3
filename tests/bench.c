#include "bench-user.h"
#include <math.h>
#include <stdio.h>

extern void timer_start(void);
extern double timer_stop(void);

/*
  horrible hack for now.  This will go away once we define an interface
  for fftw
*/
#define problem fftw_problem
#include "ifftw.h"
#include "dft.h"
#include "rdft.h"
#include "reodft.h"
#include "threads.h"
void X(dft_verify)(plan *pln, const problem_dft *p, uint rounds);
void X(rdft_verify)(plan *pln, const problem_rdft *p, uint rounds);
void X(reodft_verify)(plan *pln, const problem_rdft *p, uint rounds);
#define FFTW X
#define fftw_real R
#undef problem

/* END HACKS */

static const char *mkvers(void)
{
     return FFTW(version);
}

static const char *mkcc(void)
{
     return FFTW(cc);
}

static const char *mkcodelet_optim(void)
{
     return FFTW(codelet_optim);
}

BEGIN_BENCH_DOC
BENCH_DOC("name", "fftw3")
BENCH_DOCF("version", mkvers) 
BENCH_DOCF("fftw-compiled-by", mkcc)
BENCH_DOCF("codelet-optim", mkcodelet_optim)
END_BENCH_DOC 

static bench_real *ri, *ii, *ro, *io;
static int is, os;

void copy_c2c_from(struct problem *p, bench_complex *in)
{
     unsigned int i;
     if (p->sign == FFT_SIGN) {
	  for (i = 0; i < p->size; ++i) {
	       ri[i * is] = c_re(in[i]);
	       ii[i * is] = c_im(in[i]);
	  }
     } else {
	  for (i = 0; i < p->size; ++i) {
	       ii[i * is] = c_re(in[i]);
	       ri[i * is] = c_im(in[i]);
	  }
     }
}

void copy_c2c_to(struct problem *p, bench_complex *out)
{
     unsigned int i;
     if (p->sign == FFT_SIGN) {
	  for (i = 0; i < p->size; ++i) {
	       c_re(out[i]) = ro[i * os];
	       c_im(out[i]) = io[i * os];
	  }
     } else {
	  for (i = 0; i < p->size; ++i) {
	       c_re(out[i]) = io[i * os];
	       c_im(out[i]) = ro[i * os];
	  }
     }
}

void copy_h2c(struct problem *p, bench_complex *out)
{
     if (p->split)
	  copy_h2c_1d_halfcomplex(p, out, FFT_SIGN);
     else
	  copy_h2c_unpacked(p, out, FFT_SIGN);
}

void copy_c2h(struct problem *p, bench_complex *in)
{
     if (p->split)
	  copy_c2h_1d_halfcomplex(p, in, FFT_SIGN);
     else
	  copy_c2h_unpacked(p, in, FFT_SIGN);
}

void copy_r2c(struct problem *p, bench_complex *out)
{
     if (!p->split)
          copy_r2c_unpacked(p, out);
     else
          copy_r2c_packed(p, out);
}

void copy_c2r(struct problem *p, bench_complex *in)
{
     if (!p->split)
          copy_c2r_unpacked(p, in);
     else
          copy_c2r_packed(p, in);
}

static void hook(plan *pln, const fftw_problem *p_, int optimalp)
{
     UNUSED(optimalp);

     if (verbose > 5) {
	  printer *pr = FFTW(mkprinter_file) (stdout);
	  pr->print(pr, "%P:%(%p%)\n", p_, pln);
	  FFTW(printer_destroy) (pr);
	  printf("cost %g  \n\n", pln->pcost);
     }

     if (paranoid) {
	  if (DFTP(p_))
	       X(dft_verify)(pln, (const problem_dft *) p_, 5);
	  else if (RDFTP(p_)) {
	       X(rdft_verify)(pln, (const problem_rdft *) p_, 5);
	       X(reodft_verify)(pln, (const problem_rdft *) p_, 5);
	  }
     }
}

int can_do(struct problem *p)
{
     return (sizeof(fftw_real) == sizeof(bench_real) &&
	     (p->kind == PROBLEM_COMPLEX || p->kind == PROBLEM_REAL));
}

static planner *plnr;
static fftw_problem *prblm;
static plan *pln;

void setup(struct problem *p)
{
     double tplan;
     size_t nsize;

     BENCH_ASSERT(can_do(p));

     FFTW(threads_init)();

     plnr = FFTW(mkplanner)();
     FFTW(dft_conf_standard) (plnr);
     FFTW(rdft_conf_standard) (plnr);
     FFTW(reodft_conf_standard) (plnr);
     FFTW(threads_conf_standard) (plnr);
     plnr->nthr = 1;
     plnr->hook = hook;
     plnr->planner_flags |= NO_EXHAUSTIVE;
     plnr->planner_flags |= NO_LARGE_GENERIC;

     /* plnr->planner_flags |= IMPATIENT; */
     /* plnr->planner_flags |= ESTIMATE | IMPATIENT | NO_INDIRECT_OP; */

     if (p->kind == PROBLEM_REAL)
	  plnr->problem_flags |= DESTROY_INPUT;

#if 1
     {
	  FILE *f;
	  timer_start();
	  if ((f = fopen("wis.dat", "r"))) {
	       scanner *sc = X(mkscanner_file)(f);
	       if (!plnr->adt->imprt(plnr, sc))
		    fprintf(stderr, "bench: ERROR reading wis.dat!\n");
	       X(scanner_destroy)(sc);
	       fclose(f);
	  }

	  tplan = timer_stop();
	  {
               printer *pr = X(mkprinter_file)(stdout);
	       if (verbose)
		    pr->print(pr, "READ WISDOM (%g seconds): ", tplan);
               if (verbose > 3)
		    plnr->adt->exprt(plnr, pr);
	       if (verbose)
		    pr->print(pr, "\n");
               X(printer_destroy)(pr);
          }
     }
#endif

     if (p->kind == PROBLEM_REAL) {
	  if (p->split) {
	       is = os = 1;
	       ri = ii = p->in;
	       ro = io = p->out;
	  }
	  else if (p->sign == FFT_SIGN) {
	       is = 1; os = 2;
	       ri = ii = p->in;
	       ro = p->out; io = ro + 1;
	  }
	  else {
	       is = 2; os = 1;
	       ri = ii = p->out;
	       ro = p->in; io = ro + 1;
	  }
     }
     else if (p->split) {
	  is = os = 1;
	  if (p->sign == FFT_SIGN) {
	       ri = p->in;
	       ii = ri + p->size;
	       ro = p->out;
	       io = ro + p->size;
	  } else {
	       ii = p->in;
	       ri = ii + p->size;
	       io = p->out;
	       ro = io + p->size;
	  }
     } else {
	  is = os = 2;
	  if (p->sign == FFT_SIGN) {
	       ri = p->in;
	       ii = ri + 1;
	       ro = p->out;
	       io = ro + 1;
	  } else {
	       ii = p->in;
	       ri = ii + 1;
	       io = p->out;
	       ro = io + 1;
	  }
     }

     nsize = p->phys_size / p->vsize;
     if (p->kind == PROBLEM_COMPLEX)
	  prblm = 
	       FFTW(mkproblem_dft_d)(
		    FFTW(mktensor_rowmajor)(p->rank, p->n, p->n, p->n, is, os),
		    FFTW(mktensor_rowmajor) (p->vrank, p->vn, p->vn, p->vn,
					     is * nsize, os * nsize), 
		    ri, ii, ro, io);
     else if (p->split) {
	  CK(p->rank == 1);
	  prblm = 
	       FFTW(mkproblem_rdft_1_d)(
		    FFTW(mktensor_rowmajor)(p->rank, p->n, p->n, p->n, is, os),
		    FFTW(mktensor_rowmajor) (p->vrank, p->vn, p->vn, p->vn,
					     is * nsize, os * nsize), 
		    ri, ro, 
#if 1
		    p->sign == FFT_SIGN ? R2HC : HC2R
#else
                    RODFT00
#endif
		    ); /* emacs is confused if you duplicate the paren
			  inside #if */
     }
     else {
	  uint i, *npadr, *npadc;
	  npadr = (uint *) bench_malloc(p->rank * sizeof(uint));
	  npadc = (uint *) bench_malloc(p->rank * sizeof(uint));
	  for (i = 0; i < p->rank; ++i) npadr[i] = npadc[i] = p->n[i];
	  if (p->rank > 0)
	       npadr[p->rank-1] = 2*(npadc[p->rank-1] = npadr[p->rank-1]/2+1);
	  prblm = 
	       FFTW(mkproblem_rdft2_d)(
		    p->sign == FFT_SIGN
		    ? FFTW(mktensor_rowmajor)(p->rank, p->n, npadr, npadc, 
					      is, os)
		    : FFTW(mktensor_rowmajor)(p->rank, p->n, npadc, npadr, 
					      is, os),
		    FFTW(mktensor_rowmajor) (p->vrank, p->vn, p->vn, p->vn,
					     is * nsize, (os * nsize) / 2), 
		    ri, ro, io, 
		    p->sign == FFT_SIGN ? R2HC : HC2R);
	  bench_free(npadc);
	  bench_free(npadr);
     }
     timer_start();
     pln = plnr->adt->mkplan(plnr, prblm);
     tplan = timer_stop();
     BENCH_ASSERT(pln);

     {
	  /* tentative blessing protocol (to be implemented by API) */
	  plan *pln0;
	  plnr->planner_flags |= BLESSING;
	  pln0 = plnr->adt->mkplan(plnr, prblm);
	  X(plan_destroy)(pln0);
	  plnr->planner_flags |= ~BLESSING;
     }
	  
     if (verbose) {
	  printer *pr = FFTW(mkprinter_file) (stdout);
	  pr->print(pr, "%p\nnprob %u  nplan %u\n",
		    pln, plnr->nprob, plnr->nplan);
	  pr->print(pr, "%d add, %d mul, %d fma, %d other\n",
		    pln->ops.add, pln->ops.mul, pln->ops.fma, pln->ops.other);
	  pr->print(pr, "planner time: %g s\n", tplan);
	  if (verbose > 1) {
	       plnr->adt->exprt(plnr, pr);
	       pr->print(pr, "\n");
	  }
	  FFTW(printer_destroy)(pr);
     }
     AWAKE(pln, 1);
#if 1
     if (pln)
          hook(pln, prblm, 1);
#endif
}

void doit(int iter, struct problem *p)
{
     int i;
     plan *PLN = pln;
     fftw_problem *PRBLM = prblm;

     UNUSED(p);
     for (i = 0; i < iter; ++i) {
	  PLN->adt->solve(PLN, PRBLM);
     }
}

void done(struct problem *p)
{
     UNUSED(p);

#    ifdef FFTW_DEBUG
     if (verbose >= 2)
	  FFTW(planner_dump)(plnr, verbose - 2);
#    endif

     AWAKE(pln, 0);
     FFTW(plan_destroy) (pln);
     FFTW(problem_destroy) (prblm);

#if 1
     {
	  FILE *f;
	  if ((f = fopen("wis.dat", "w"))) {
	       printer *pr = X(mkprinter_file)(f);
	       plnr->adt->exprt(plnr, pr);
	       X(printer_destroy)(pr);
	       fclose(f);
	  }
     }
#endif

     FFTW(planner_destroy) (plnr);

#    ifdef FFTW_DEBUG
     if (verbose >= 1)
	  FFTW(malloc_print_minfo)();
#    endif
}
