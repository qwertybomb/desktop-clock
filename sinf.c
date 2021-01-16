/*							sinf.c
 *
 *	Circular sine
 *
 *
 *
 * SYNOPSIS:
 *
 * float x, y, sinf();
 *
 * y = sinf( x );
 *
 *
 *
 * DESCRIPTION:
 *
 * Range reduction is into intervals of pi/4.  The reduction
 * error is nearly eliminated by contriving an extended precision
 * modular arithmetic.
 *
 * Two polynomial approximating functions are employed.
 * Between 0 and pi/4 the sine is approximated by
 *      x  +  x**3 P(x**2).
 * Between pi/4 and pi/2 the cosine is represented as
 *      1  -  x**2 Q(x**2).
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain      # trials      peak       rms
 *    IEEE    -4096,+4096   100,000      1.2e-7     3.0e-8
 *    IEEE    -8192,+8192   100,000      3.0e-7     3.0e-8
 * 
 * ERROR MESSAGES:
 *
 *   message           condition        value returned
 * sin total loss      x > 2^24              0.0
 *
 * Partial loss of accuracy begins to occur at x = 2^13
 * = 8192. Results may be meaningless for x >= 2^24
 * The routine as implemented flags a TLOSS error
 * for x >= 2^24 and returns 0.0.
 */

/*							cosf.c
 *
 *	Circular cosine
 *
 *
 *
 * SYNOPSIS:
 *
 * float x, y, cosf();
 *
 * y = cosf( x );
 *
 *
 *
 * DESCRIPTION:
 *
 * Range reduction is into intervals of pi/4.  The reduction
 * error is nearly eliminated by contriving an extended precision
 * modular arithmetic.
 *
 * Two polynomial approximating functions are employed.
 * Between 0 and pi/4 the cosine is approximated by
 *      1  -  x**2 Q(x**2).
 * Between pi/4 and pi/2 the sine is represented as
 *      x  +  x**3 P(x**2).
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain      # trials      peak         rms
 *    IEEE    -8192,+8192   100,000      3.0e-7     3.0e-8
 */

/*
Cephes Math Library Release 2.2:  June, 1992
Copyright 1985, 1987, 1988, 1992 by Stephen L. Moshier
Direct inquiries to 30 Frost Street, Cambridge, MA 02140
*/


/* Single precision circular sine
 * test interval: [-pi/4, +pi/4]
 * trials: 10000
 * peak relative error: 6.8e-8
 * rms relative error: 2.6e-8
 */

static float const FOPI = 1.27323954473516f;

/* These are for a 24-bit significand: */
static float const DP1 = 0.78515625f;
static float const DP2 = 2.4187564849853515625e-4f;
static float const DP3 = 3.77489497744594108e-8f;

static float const sincof[] = {
    -1.9515295891E-4f,
    8.3321608736E-3f,
    -1.6666654611E-1f
};

static float const coscof[] = {
    2.443315711809948E-005f,
    -1.388731625493765E-003f,
    4.166664568298827E-002f
};

float I_sinf(float xx)
{
    float const *p;
    float x, y, z;
    int unsigned j;
    int sign;

    sign = 1;
    x = xx;

    if (xx < 0)
    {
        sign = -1;
        x = -xx;
    }

    j = (int unsigned) (FOPI * x); /* integer part of x/(PI/4) */
    y = (float) j;

    /* map zeros to origin */
    if (j & 1)
    {
        j += 1;
        y += 1.0f;
    }
    j &= 7; /* octant modulo 360 degrees */

    /* reflect in x axis */
    if (j > 3)
    {
        sign = -sign;
        j -= 4;
    }


    /* Extended precision modular arithmetic */
    x = ((x - y * DP1) - y * DP2) - y * DP3;

    z = x * x;
    if ((j == 1) || (j == 2))
    {
        /* measured relative error in +/- pi/4 is 7.8e-8 */
        /*
	        y = ((  2.443315711809948E-005 * z
	        - 1.388731625493765E-003) * z
	        + 4.166664568298827E-002) * z * z;
        */
        p = coscof;
        y = *p++;
        y = y * z + *p++;
        y = y * z + *p;
        y *= z * z;
        y -= 0.5f * z;
        y += 1.0f;
    }
    else
    {
    /* Theoretical relative error = 3.8e-9 in [-pi/4, +pi/4] */
    /*
	    y = ((-1.9515295891E-4 * z
	       + 8.3321608736E-3) * z
	         - 1.6666654611E-1) * z * x;
	    y += x;
    */
        p = sincof;
        y = *p++;
        y = y * z + *p++;
        y = y * z + *p;
        y *= z * x;
        y += x;
    }

    if (sign < 0)
    {
        y = -y;
    }

    return (y);
}


/* Single precision circular cosine
 * test interval: [-pi/4, +pi/4]
 * trials: 10000
 * peak relative error: 8.3e-8
 * rms relative error: 2.2e-8
 */

float I_cosf( float xx )
{
    float x, y, z;
    int j, sign;

    /* make argument positive */
    sign = 1;
    x = xx;
    if (x < 0)
    {
        x = -x;
    }

    j = (int) (FOPI * x); /* integer part of x/PIO4 */
    y = (float) j;

    /* integer and fractional part modulo one octant */
    if (j & 1)    /* map zeros to origin */
    {
        j += 1;
        y += 1.0f;
    }

    j &= 7;
    if (j > 3)
    {
        j -= 4;
        sign = -sign;
    }

    if (j > 1)
    {
        sign = -sign;
    }

    /* Extended precision modular arithmetic */
    x = ((x - y * DP1) - y * DP2) - y * DP3;

    z = x * x;

    if ((j == 1) || (j == 2))
    {
        y = (((-1.9515295891E-4f * z
               + 8.3321608736E-3f) * z
              - 1.6666654611E-1f) * z * x)
            + x;
    }
    else
    {
        y = ((2.443315711809948E-005f * z
              - 1.388731625493765E-003f) * z
             + 4.166664568298827E-002f) * z * z;
        y -= 0.5f * z;
        y += 1.0f;
    }
    if (sign < 0)
    {
        y = -y;
    }

    return (y);
}
