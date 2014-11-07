#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#define FRAC      (1 << 14)    /* FRACtion part of fixed-point 17:14 number */
#define TO_FP(n)  ((n) * FRAC) /* Convert n to fixed point */
#define TO_INT(x) ((x) / FRAC) /* Convert x to integer (rounding toward zero)*/
#define ROUND_TO_INT(x) ((x) >= 0 ? ((x) + FRAC/2) / FRAC : ((x) - FRAC/2) / FRAC)
                               /* Convert x to integer (rounding to nearest) */

#define FADD_F(x,y) ((x) + (y))                   /* Add x and y  */
#define FSUB_F(x,y) ((x) - (y))                   /* Subtract y from x  */
#define FMUL_F(x,y) ((int64_t) (x) * (y) / FRAC)  /* Multiply x by y  */
#define FDIV_F(x,y) ((int64_t) (x) * FRAC / (y))  /* Divide x by y  */

#define FADD_I(x,n) ((x) + (n) * FRAC)  /* Add x and n  */
#define FSUB_I(x,n) ((x) - (n) * FRAC)  /* Subtract n from x  */
#define FMUL_I(x,n) ((x) * (n))         /* Multiply x by n  */
#define FDIV_I(x,n) ((x) / (n))         /* Divide x by n */

#endif /* threads/fixed-point.h*/
