/*!
	@file sse_mathfun.h

	SIMD (SSE1+MMX or SSE2) implementation of sin, cos, exp and log

   Inspired by Intel Approximate Math library, and based on the
   corresponding algorithms of the cephes math library

   The default is to use the SSE1 version. If you define USE_SSE2 the
   the SSE2 intrinsics will be used in place of the MMX intrinsics. Do
   not expect any significant performance improvement with SSE2.
*/

/* Copyright (C) 2010,2011  RJVB - extensions */
/* Copyright (C) 2007  Julien Pommier

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  (this is the zlib license)
*/

#ifndef _SSE_MATHFUN_H

#ifdef USE_SSE_AUTO
#	ifdef __SSE2__
#		if defined(__GNUC__)
#			warning "USE_SSE2"
#		endif
#		define USE_SSE2
#	endif
#	if defined(__SSE3__) || defined(__SSSE3__)
#		if defined(__GNUC__)
#			warning "USE_SSE3"
#		endif
#		define USE_SSE2
#		define USE_SSE3
#	endif
#	if defined(__SSE4__) || defined(__SSE4_1__) || defined(__SSE4_2__) || ((_M_IX86_FP > 1) && !defined(_M_AMD64))
#		if defined(__GNUC__)
#			warning "USE_SSE4"
#		endif
#		define USE_SSE2
#		define USE_SSE3
#		define USE_SSE4
#	endif
#endif

#include <math.h>
#include <xmmintrin.h>
#include <emmintrin.h>

/* yes I know, the top of this file is quite ugly */

/*!
	macros to obtain the required 16bit alignment
 */
#ifdef _MSC_VER /* visual c++ */
# define ALIGN16_BEG __declspec(align(16))
# define ALIGN16_END
# define inline	__forceinline
#else /* gcc or icc */
# define ALIGN16_BEG
# define ALIGN16_END __attribute__((aligned(16)))
#endif

/* __m128 is ugly to write */
/*!
	an SSE vector of 4 floats
 */
typedef __m128 v4sf;  // vector of 4 float (sse1)

#if defined(USE_SSE3) || defined(USE_SSE4)
#	define USE_SSE2
#endif

/*!
	an SSE/MMX vector of 4 32bit integers
 */
#ifdef __APPLE_CC__
	typedef int	v4si __attribute__ ((__vector_size__ (16), __may_alias__));
#else
	typedef __m128i v4si; // vector of 4 int (sse2)
#endif
// RJVB 20111028: some support for double precision semantics
/*!
	an SSE2+ vector of 2 doubles
 */
typedef __m128d v2df; // vector of 2 double (sse2)
/*!
	an MMX vector of 2 32bit ints
 */
typedef __m64 v2si;   // vector of 2 int (mmx)

#if defined(USE_SSE3) || defined(USE_SSE4)
#	define USE_SSE3
#	include <pmmintrin.h>
#	if defined(__SSSE3__) || (_M_IX86_FP > 1)
#		include <tmmintrin.h>
#	endif
#endif

#if defined(USE_SSE4)
#	define USE_SSE4
#	include <smmintrin.h>
#endif

#ifdef __GNUC__0
#	define _MM_SET_PD(b,a)		(v2df){(a),(b)}
#	define _MM_SET1_PD(a)		(v2df){(a),(a)}
// 	static inline v2df _MM_SET1_PD(double a)
// 	{
// 		return (v2df){a,a};
// 	}
#	define _MM_SETR_PD(a,b)		(v2df){(a),(b)}
#	define _MM_SETZERO_PD()		(v2df){0.0,0.0}
#	define _MM_SET_PS(d,c,b,a)	(v4sf){(a),(b),(c),(d)}
#	define _MM_SET1_PS(a)		(v4sf){(a),(a),(a),(a)}
// 	static inline v4sf _MM_SET1_PS(float a)
// 	{
// 		return (v4sf){a,a,a,a};
// 	}
#	define _MM_SETR_PS(a,b,c,d)	(v4sf){(a),(b),(c),(d)}
#	define _MM_SETZERO_PS()		(v4sf){0.0f,0.0f,0.0f,0.0f}
#	define _MM_SETZERO_SI128()	(__m128i)(__v4si){0,0,0,0}
#	define _MM_SETZERO_SI64()	ALIGN16_BEG (__m64 ALIGN16_END)0LL
#else
#	define _MM_SET_PD(b,a)		_mm_setr_pd((a),(b))
#	define _MM_SET1_PD(a)		_mm_set1_pd((a))
#	define _MM_SETR_PD(a,b)		_mm_setr_pd((a),(b))
#	define _MM_SETZERO_PD()		_mm_setzero_pd()
#	define _MM_SET_PS(d,c,b,a)	_mm_setr_ps((a),(b),(c),(d))
#	define _MM_SET1_PS(a)		_mm_set1_ps((a))
#	define _MM_SETR_PS(a,b,c,d)	_mm_setr_ps((a),(b),(c),(d))
#	define _MM_SETZERO_PS()		_mm_setzero_ps()
#	define _MM_SETZERO_SI128()	_mm_setzero_si128()
#	define _MM_SETZERO_SI64()	_mm_setzero_si64()
#endif
#define VELEM(type,a,n)			(((type*)&a)[n])

/* declare some SSE constants -- why can't I figure a better way to do that? */
#define _PS_CONST(Name, Val)                                            \
  static const ALIGN16_BEG float _ps_##Name[4] ALIGN16_END = { (const float)(Val), (const float)(Val), (const float)(Val), (const float)(Val) }
#define _PI32_CONST(Name, Val)                                            \
  static const ALIGN16_BEG int _pi32_##Name[4] ALIGN16_END = { Val, Val, Val, Val }
#define _PS_CONST_TYPE(Name, Type, Val)                                 \
  static const ALIGN16_BEG Type _ps_##Name[4] ALIGN16_END = { (Type)Val, (Type)Val, (Type)Val, (Type)Val }

// static const int _ps_sign_mask[4] __attribute__((aligned(16))) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };

#define _PD_CONST(Name, Val)                                            \
	static const ALIGN16_BEG double _pd_##Name[2] ALIGN16_END = { (const double)(Val), (const double)(Val) }
#define _PD_CONST_TYPE(Name, Type, Val)                                 \
	static const ALIGN16_BEG Type _pd_##Name[2] ALIGN16_END = { (Type)Val, (Type)Val }

#ifdef SSE_MATHFUN_WITH_CODE

_PS_CONST(1  , 1.0f);
_PS_CONST(0p5, 0.5f);
/* the smallest non denormalized float number */
_PS_CONST_TYPE(min_norm_pos, int, 0x00800000);
_PS_CONST_TYPE(mant_mask, int, 0x7f800000);
_PS_CONST_TYPE(inv_mant_mask, int, ~0x7f800000);

_PS_CONST_TYPE(sign_mask, int, 0x80000000);
_PS_CONST_TYPE(inv_sign_mask, int, ~0x80000000);

_PI32_CONST(1, 1);
_PI32_CONST(inv1, ~1);
_PI32_CONST(2, 2);
_PI32_CONST(4, 4);
_PI32_CONST(0x7f, 0x7f);

_PS_CONST(cephes_SQRTHF, 0.707106781186547524);
_PS_CONST(cephes_log_p0, 7.0376836292E-2);
_PS_CONST(cephes_log_p1, - 1.1514610310E-1);
_PS_CONST(cephes_log_p2, 1.1676998740E-1);
_PS_CONST(cephes_log_p3, - 1.2420140846E-1);
_PS_CONST(cephes_log_p4, + 1.4249322787E-1);
_PS_CONST(cephes_log_p5, - 1.6668057665E-1);
_PS_CONST(cephes_log_p6, + 2.0000714765E-1);
_PS_CONST(cephes_log_p7, - 2.4999993993E-1);
_PS_CONST(cephes_log_p8, + 3.3333331174E-1);
_PS_CONST(cephes_log_q1, -2.12194440e-4);
_PS_CONST(cephes_log_q2, 0.693359375);

#ifdef USE_SSE2
	_PD_CONST(1, 1.0);
	_PD_CONST(_1, -1.0);
	_PD_CONST(0p5, 0.5);
	/* the smallest non denormalised float number */
//	_PD_CONST_TYPE(min_norm_pos, int, 0x00800000);
//	_PD_CONST_TYPE(mant_mask, int, 0x7f800000);
//	_PD_CONST_TYPE(inv_mant_mask, int, ~0x7f800000);

	_PD_CONST_TYPE(sign_mask, long long, 0x8000000000000000LL);
	_PD_CONST_TYPE(inv_sign_mask, long long, ~0x8000000000000000LL);

#endif

#if defined (__MINGW32__)

/* the ugly part below: many versions of gcc used to be completely buggy with respect to some intrinsics
   The movehl_ps is fixed in mingw 3.4.5, but I found out that all the _mm_cmp* intrinsics were completely
   broken on my mingw gcc 3.4.5 ...

   Note that the bug on _mm_cmp* does occur only at -O0 optimization level
*/

inline __m128 my_movehl_ps(__m128 a, const __m128 b) {
	asm (
			"movhlps %2,%0\n\t"
			: "=x" (a)
			: "0" (a), "x"(b)
	    );
	return a;                                 }
#warning "redefined _mm_movehl_ps (see gcc bug 21179)"
#define _mm_movehl_ps my_movehl_ps

inline __m128 my_cmplt_ps(__m128 a, const __m128 b) {
	asm (
			"cmpltps %2,%0\n\t"
			: "=x" (a)
			: "0" (a), "x"(b)
	    );
	return a;
                  }
inline __m128 my_cmpgt_ps(__m128 a, const __m128 b) {
	asm (
			"cmpnleps %2,%0\n\t"
			: "=x" (a)
			: "0" (a), "x"(b)
	    );
	return a;
}
inline __m128 my_cmpeq_ps(__m128 a, const __m128 b) {
	asm (
			"cmpeqps %2,%0\n\t"
			: "=x" (a)
			: "0" (a), "x"(b)
	    );
	return a;
}
#warning "redefined _mm_cmpxx_ps functions..."
#define _mm_cmplt_ps my_cmplt_ps
#define _mm_cmpgt_ps my_cmpgt_ps
#define _mm_cmpeq_ps my_cmpeq_ps
#endif

#ifndef USE_SSE2
typedef union xmm_mm_union {
  __m128 xmm;
  __m64 mm[2];
} xmm_mm_union;

#define COPY_XMM_TO_MM(xmm_, mm0_, mm1_) {          \
    xmm_mm_union u; u.xmm = xmm_;                   \
    mm0_ = u.mm[0];                                 \
    mm1_ = u.mm[1];                                 \
}

#define COPY_MM_TO_XMM(mm0_, mm1_, xmm_) {                         \
    xmm_mm_union u; u.mm[0]=mm0_; u.mm[1]=mm1_; xmm_ = u.xmm;      \
  }

#endif // USE_SSE2

/*!
	natural logarithm computed for 4 simultaneous float
	@n
   return NaN for x <= 0
*/
static inline v4sf log_ps(v4sf x)
{
  v4sf e;
#ifdef USE_SSE2
  v4si emm0;
#else
  v2si mm0, mm1;
#endif
  v4sf one = *(v4sf*)_ps_1;
  v4sf invalid_mask = _mm_cmple_ps(x, _MM_SETZERO_PS());

  x = _mm_max_ps(x, *(v4sf*)_ps_min_norm_pos);  /* cut off denormalized stuff */

#ifndef USE_SSE2
  /* part 1: x = frexpf(x, &e); */
  COPY_XMM_TO_MM(x, mm0, mm1);
  mm0 = _mm_srli_pi32(mm0, 23);
  mm1 = _mm_srli_pi32(mm1, 23);
#else
  emm0 = _mm_srli_epi32(_mm_castps_si128(x), 23);
#endif
  /* keep only the fractional part */
  x = _mm_and_ps(x, *(v4sf*)_ps_inv_mant_mask);
  x = _mm_or_ps(x, *(v4sf*)_ps_0p5);

#ifndef USE_SSE2
  /* now e=mm0:mm1 contain the really base-2 exponent */
  mm0 = _mm_sub_pi32(mm0, *(v2si*)_pi32_0x7f);
  mm1 = _mm_sub_pi32(mm1, *(v2si*)_pi32_0x7f);
  e = _mm_cvtpi32x2_ps(mm0, mm1);
  _mm_empty(); /* bye bye mmx */
#else
  emm0 = _mm_sub_epi32(emm0, *(v4si*)_pi32_0x7f);
  e = _mm_cvtepi32_ps(emm0);
#endif

  e = _mm_add_ps(e, one);

  /* part2:
     if( x < SQRTHF ) {
       e -= 1;
       x = x + x - 1.0;
     } else { x = x - 1.0; }
  */
  {
	  v4sf z, y;
	  v4sf mask = _mm_cmplt_ps(x, *(v4sf*)_ps_cephes_SQRTHF);
	  v4sf tmp = _mm_and_ps(x, mask);
	  x = _mm_sub_ps(x, one);
	  e = _mm_sub_ps(e, _mm_and_ps(one, mask));
	  x = _mm_add_ps(x, tmp);


	  z = _mm_mul_ps(x,x);

	  y = *(v4sf*)_ps_cephes_log_p0;
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p1);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p2);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p3);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p4);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p5);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p6);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p7);
	  y = _mm_mul_ps(y, x);
	  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p8);
	  y = _mm_mul_ps(y, x);

	  y = _mm_mul_ps(y, z);

	  tmp = _mm_mul_ps(e, *(v4sf*)_ps_cephes_log_q1);
	  y = _mm_add_ps(y, tmp);


	  tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
	  y = _mm_sub_ps(y, tmp);

	  tmp = _mm_mul_ps(e, *(v4sf*)_ps_cephes_log_q2);
	  x = _mm_add_ps(x, y);
	  x = _mm_add_ps(x, tmp);
	  x = _mm_or_ps(x, invalid_mask); // negative arg will be NAN
  }
  return x;
}

_PS_CONST(exp_hi,	88.3762626647949f);
_PS_CONST(exp_lo,	-88.3762626647949f);

_PS_CONST(cephes_LOG2EF, 1.44269504088896341);
_PS_CONST(cephes_exp_C1, 0.693359375);
_PS_CONST(cephes_exp_C2, -2.12194440e-4);

_PS_CONST(cephes_exp_p0, 1.9875691500E-4);
_PS_CONST(cephes_exp_p1, 1.3981999507E-3);
_PS_CONST(cephes_exp_p2, 8.3334519073E-3);
_PS_CONST(cephes_exp_p3, 4.1665795894E-2);
_PS_CONST(cephes_exp_p4, 1.6666665459E-1);
_PS_CONST(cephes_exp_p5, 5.0000001201E-1);

/*!
	computes e**x of the 4 floats in x
 */
static inline v4sf exp_ps(v4sf x)
{ v4sf tmp = _MM_SETZERO_PS(), fx, mask, y, z;
  v4sf pow2n;
#ifdef USE_SSE2
  v4si emm0;
#else
  v2si mm0, mm1;
#endif
  v4sf one = *(v4sf*)_ps_1;

  x = _mm_min_ps(x, *(v4sf*)_ps_exp_hi);
  x = _mm_max_ps(x, *(v4sf*)_ps_exp_lo);

  /* express exp(x) as exp(g + n*log(2)) */
  fx = _mm_mul_ps(x, *(v4sf*)_ps_cephes_LOG2EF);
  fx = _mm_add_ps(fx, *(v4sf*)_ps_0p5);

  /* how to perform a floorf with SSE: just below */
#ifndef USE_SSE2
  /* step 1 : cast to int */
  tmp = _mm_movehl_ps(tmp, fx);
  mm0 = _mm_cvttps_pi32(fx);
  mm1 = _mm_cvttps_pi32(tmp);
  /* step 2 : cast back to float */
  tmp = _mm_cvtpi32x2_ps(mm0, mm1);
#else
  emm0 = _mm_cvttps_epi32(fx);
  tmp  = _mm_cvtepi32_ps(emm0);
#endif
  /* if greater, substract 1 */
  mask = _mm_cmpgt_ps(tmp, fx);
  mask = _mm_and_ps(mask, one);
  fx = _mm_sub_ps(tmp, mask);

  tmp = _mm_mul_ps(fx, *(v4sf*)_ps_cephes_exp_C1);
  z = _mm_mul_ps(fx, *(v4sf*)_ps_cephes_exp_C2);
  x = _mm_sub_ps(x, tmp);
  x = _mm_sub_ps(x, z);

  z = _mm_mul_ps(x,x);

  y = *(v4sf*)_ps_cephes_exp_p0;
  y = _mm_mul_ps(y, x);
  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p1);
  y = _mm_mul_ps(y, x);
  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p2);
  y = _mm_mul_ps(y, x);
  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p3);
  y = _mm_mul_ps(y, x);
  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p4);
  y = _mm_mul_ps(y, x);
  y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p5);
  y = _mm_mul_ps(y, z);
  y = _mm_add_ps(y, x);
  y = _mm_add_ps(y, one);

  /* build 2^n */
#ifndef USE_SSE2
  z = _mm_movehl_ps(z, fx);
  mm0 = _mm_cvttps_pi32(fx);
  mm1 = _mm_cvttps_pi32(z);
  mm0 = _mm_add_pi32(mm0, *(v2si*)_pi32_0x7f);
  mm1 = _mm_add_pi32(mm1, *(v2si*)_pi32_0x7f);
  mm0 = _mm_slli_pi32(mm0, 23);
  mm1 = _mm_slli_pi32(mm1, 23);

  COPY_MM_TO_XMM(mm0, mm1, pow2n);
  _mm_empty();
#else
  emm0 = _mm_cvttps_epi32(fx);
  emm0 = _mm_add_epi32(emm0, *(v4si*)_pi32_0x7f);
  emm0 = _mm_slli_epi32(emm0, 23);
  pow2n = _mm_castsi128_ps(emm0);
#endif
  y = _mm_mul_ps(y, pow2n);
  return y;
}

_PS_CONST(minus_cephes_DP1, -0.78515625);
_PS_CONST(minus_cephes_DP2, -2.4187564849853515625e-4);
_PS_CONST(minus_cephes_DP3, -3.77489497744594108e-8);
_PS_CONST(sincof_p0, -1.9515295891E-4);
_PS_CONST(sincof_p1,  8.3321608736E-3);
_PS_CONST(sincof_p2, -1.6666654611E-1);
_PS_CONST(coscof_p0,  2.443315711809948E-005);
_PS_CONST(coscof_p1, -1.388731625493765E-003);
_PS_CONST(coscof_p2,  4.166664568298827E-002);
_PS_CONST(cephes_FOPI, 1.27323954473516); // 4 / M_PI

#ifdef USE_SSE2
	_PD_CONST(minus_cephes_DP1, -0.78515625);
	_PD_CONST(minus_cephes_DP2, -2.4187564849853515625e-4);
	_PD_CONST(minus_cephes_DP3, -3.77489497744594108e-8);
	_PD_CONST(sincof_p0, -1.9515295891E-4);
	_PD_CONST(sincof_p1,  8.3321608736E-3);
	_PD_CONST(sincof_p2, -1.6666654611E-1);
	_PD_CONST(coscof_p0,  2.443315711809948E-005);
	_PD_CONST(coscof_p1, -1.388731625493765E-003);
	_PD_CONST(coscof_p2,  4.166664568298827E-002);
	_PD_CONST(cephes_FOPI, 1.27323954473516); // 4 / M_PI
#endif


/*!
	evaluation of 4 sines at onces, using only SSE1+MMX intrinsics so
   it runs also on old athlons XPs and the pentium III of your grand
   mother.
	@n
   The code is the exact rewriting of the cephes sinf function.
   Precision is excellent as long as x < 8192 (I did not bother to
   take into account the special handling they have for greater values
   -- it does not return garbage for arguments over 8192, though, but
   the extra precision is missing).
	@n
   Note that it is such that sinf((float)M_PI) = 8.74e-8, which is the
   surprising but correct result.
	@n
   Performance is also surprisingly good, 1.33 times faster than the
   macos vsinf SSE2 function, and 1.5 times faster than the
   __vrs4_sinf of amd's ACML (which is only available in 64 bits). Not
   too bad for an SSE1 function (with no special tuning) !
   However the latter libraries probably have a much better handling of NaN,
   Inf, denormalized and other special arguments..
	@n
   On my core 1 duo, the execution of this function takes approximately 95 cycles.
	@n
   From what I have observed on the experiments with Intel AMath lib, switching to an
   SSE2 version would improve the perf by only 10%.
	@n
   Since it is based on SSE intrinsics, it has to be compiled at -O2 to
   deliver full speed.
*/
static inline v4sf sin_ps(v4sf x)
{ // any x
  v4sf xmm1, xmm2 = _MM_SETZERO_PS(), xmm3, sign_bit, y, y2, z, tmp;

  v4sf swap_sign_bit, poly_mask;
#ifdef USE_SSE2
  v4si emm0, emm2;
#else
  v2si mm0, mm1, mm2, mm3;
#endif
  sign_bit = x;
  /* take the absolute value */
  x = _mm_and_ps(x, *(v4sf*)_ps_inv_sign_mask);
  /* extract the sign bit (upper one) */
  sign_bit = _mm_and_ps(sign_bit, *(v4sf*)_ps_sign_mask);

  /* scale by 4/Pi */
  y = _mm_mul_ps(x, *(v4sf*)_ps_cephes_FOPI);

  //printf("plop:"); print4(y);
#ifdef USE_SSE2
  /* store the integer part of y in mm0 */
  emm2 = _mm_cvttps_epi32(y);
  /* j=(j+1) & (~1) (see the cephes sources) */
  emm2 = _mm_add_epi32(emm2, *(v4si*)_pi32_1);
  emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_inv1);
  y = _mm_cvtepi32_ps(emm2);
  /* get the swap sign flag */
  emm0 = _mm_and_si128(emm2, *(v4si*)_pi32_4);
  emm0 = _mm_slli_epi32(emm0, 29);
  /* get the polynom selection mask
     there is one polynom for 0 <= x <= Pi/4
     and another one for Pi/4<x<=Pi/2

     Both branches will be computed.
  */
  emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_2);
  emm2 = _mm_cmpeq_epi32(emm2, _MM_SETZERO_SI128());

  swap_sign_bit = _mm_castsi128_ps(emm0);
  poly_mask = _mm_castsi128_ps(emm2);
  sign_bit = _mm_xor_ps(sign_bit, swap_sign_bit);
#else
  /* store the integer part of y in mm0:mm1 */
  xmm2 = _mm_movehl_ps(xmm2, y);
  mm2 = _mm_cvttps_pi32(y);
  mm3 = _mm_cvttps_pi32(xmm2);
  /* j=(j+1) & (~1) (see the cephes sources) */
  mm2 = _mm_add_pi32(mm2, *(v2si*)_pi32_1);
  mm3 = _mm_add_pi32(mm3, *(v2si*)_pi32_1);
  mm2 = _mm_and_si64(mm2, *(v2si*)_pi32_inv1);
  mm3 = _mm_and_si64(mm3, *(v2si*)_pi32_inv1);
  y = _mm_cvtpi32x2_ps(mm2, mm3);
  /* get the swap sign flag */
  mm0 = _mm_and_si64(mm2, *(v2si*)_pi32_4);
  mm1 = _mm_and_si64(mm3, *(v2si*)_pi32_4);
  mm0 = _mm_slli_pi32(mm0, 29);
  mm1 = _mm_slli_pi32(mm1, 29);
  /* get the polynom selection mask */
  mm2 = _mm_and_si64(mm2, *(v2si*)_pi32_2);
  mm3 = _mm_and_si64(mm3, *(v2si*)_pi32_2);
  mm2 = _mm_cmpeq_pi32(mm2, _MM_SETZERO_SI64());
  mm3 = _mm_cmpeq_pi32(mm3, _MM_SETZERO_SI64());
  COPY_MM_TO_XMM(mm0, mm1, swap_sign_bit);
  COPY_MM_TO_XMM(mm2, mm3, poly_mask);
  sign_bit = _mm_xor_ps(sign_bit, swap_sign_bit);
  _mm_empty(); /* good-bye mmx */
#endif

  /* The magic pass: "Extended precision modular arithmetic"
     x = ((x - y * DP1) - y * DP2) - y * DP3; */
  xmm1 = *(v4sf*)_ps_minus_cephes_DP1;
  xmm2 = *(v4sf*)_ps_minus_cephes_DP2;
  xmm3 = *(v4sf*)_ps_minus_cephes_DP3;
  xmm1 = _mm_mul_ps(y, xmm1);
  xmm2 = _mm_mul_ps(y, xmm2);
  xmm3 = _mm_mul_ps(y, xmm3);
  x = _mm_add_ps(x, xmm1);
  x = _mm_add_ps(x, xmm2);
  x = _mm_add_ps(x, xmm3);

  /* Evaluate the first polynom  (0 <= x <= Pi/4) */
  y = *(v4sf*)_ps_coscof_p0;
  z = _mm_mul_ps(x,x);

  y = _mm_mul_ps(y, z);
  y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p1);
  y = _mm_mul_ps(y, z);
  y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p2);
  y = _mm_mul_ps(y, z);
  y = _mm_mul_ps(y, z);
  tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
  y = _mm_sub_ps(y, tmp);
  y = _mm_add_ps(y, *(v4sf*)_ps_1);

  /* Evaluate the second polynom  (Pi/4 <= x <= 0) */

  y2 = *(v4sf*)_ps_sincof_p0;
  y2 = _mm_mul_ps(y2, z);
  y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p1);
  y2 = _mm_mul_ps(y2, z);
  y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p2);
  y2 = _mm_mul_ps(y2, z);
  y2 = _mm_mul_ps(y2, x);
  y2 = _mm_add_ps(y2, x);

  /* select the correct result from the two polynoms */
  xmm3 = poly_mask;
  y2 = _mm_and_ps(xmm3, y2); //, xmm3);
  y = _mm_andnot_ps(xmm3, y);
  y = _mm_add_ps(y,y2);
  /* update the sign */
  y = _mm_xor_ps(y, sign_bit);

  return y;
}

/*!
	almost the same as sin_ps
 */
v4sf cos_ps(v4sf x)
{ // any x
  v4sf xmm1, xmm2 = _MM_SETZERO_PS(), xmm3, y, y2, z, sign_bit, poly_mask, tmp;
#ifdef USE_SSE2
  v4si emm0, emm2;
#else
  v2si mm0, mm1, mm2, mm3;
#endif
  /* take the absolute value */
  x = _mm_and_ps(x, *(v4sf*)_ps_inv_sign_mask);

  /* scale by 4/Pi */
  y = _mm_mul_ps(x, *(v4sf*)_ps_cephes_FOPI);

#ifdef USE_SSE2
  /* store the integer part of y in mm0 */
  emm2 = _mm_cvttps_epi32(y);
  /* j=(j+1) & (~1) (see the cephes sources) */
  emm2 = _mm_add_epi32(emm2, *(v4si*)_pi32_1);
  emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_inv1);
  y = _mm_cvtepi32_ps(emm2);

  emm2 = _mm_sub_epi32(emm2, *(v4si*)_pi32_2);

  /* get the swap sign flag */
  emm0 = _mm_andnot_si128(emm2, *(v4si*)_pi32_4);
  emm0 = _mm_slli_epi32(emm0, 29);
  /* get the polynom selection mask */
  emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_2);
  emm2 = _mm_cmpeq_epi32(emm2, _MM_SETZERO_SI128());

  sign_bit = _mm_castsi128_ps(emm0);
  poly_mask = _mm_castsi128_ps(emm2);
#else
  /* store the integer part of y in mm0:mm1 */
  xmm2 = _mm_movehl_ps(xmm2, y);
  mm2 = _mm_cvttps_pi32(y);
  mm3 = _mm_cvttps_pi32(xmm2);

  /* j=(j+1) & (~1) (see the cephes sources) */
  mm2 = _mm_add_pi32(mm2, *(v2si*)_pi32_1);
  mm3 = _mm_add_pi32(mm3, *(v2si*)_pi32_1);
  mm2 = _mm_and_si64(mm2, *(v2si*)_pi32_inv1);
  mm3 = _mm_and_si64(mm3, *(v2si*)_pi32_inv1);

  y = _mm_cvtpi32x2_ps(mm2, mm3);


  mm2 = _mm_sub_pi32(mm2, *(v2si*)_pi32_2);
  mm3 = _mm_sub_pi32(mm3, *(v2si*)_pi32_2);

  /* get the swap sign flag in mm0:mm1 and the
     polynom selection mask in mm2:mm3 */

  mm0 = _mm_andnot_si64(mm2, *(v2si*)_pi32_4);
  mm1 = _mm_andnot_si64(mm3, *(v2si*)_pi32_4);
  mm0 = _mm_slli_pi32(mm0, 29);
  mm1 = _mm_slli_pi32(mm1, 29);

  mm2 = _mm_and_si64(mm2, *(v2si*)_pi32_2);
  mm3 = _mm_and_si64(mm3, *(v2si*)_pi32_2);

  mm2 = _mm_cmpeq_pi32(mm2, _MM_SETZERO_SI64());
  mm3 = _mm_cmpeq_pi32(mm3, _MM_SETZERO_SI64());

  COPY_MM_TO_XMM(mm0, mm1, sign_bit);
  COPY_MM_TO_XMM(mm2, mm3, poly_mask);
  _mm_empty(); /* good-bye mmx */
#endif
  /* The magic pass: "Extended precision modular arithmetic"
     x = ((x - y * DP1) - y * DP2) - y * DP3; */
  xmm1 = *(v4sf*)_ps_minus_cephes_DP1;
  xmm2 = *(v4sf*)_ps_minus_cephes_DP2;
  xmm3 = *(v4sf*)_ps_minus_cephes_DP3;
  xmm1 = _mm_mul_ps(y, xmm1);
  xmm2 = _mm_mul_ps(y, xmm2);
  xmm3 = _mm_mul_ps(y, xmm3);
  x = _mm_add_ps(x, xmm1);
  x = _mm_add_ps(x, xmm2);
  x = _mm_add_ps(x, xmm3);

  /* Evaluate the first polynom  (0 <= x <= Pi/4) */
  y = *(v4sf*)_ps_coscof_p0;
  z = _mm_mul_ps(x,x);

  y = _mm_mul_ps(y, z);
  y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p1);
  y = _mm_mul_ps(y, z);
  y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p2);
  y = _mm_mul_ps(y, z);
  y = _mm_mul_ps(y, z);
  tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
  y = _mm_sub_ps(y, tmp);
  y = _mm_add_ps(y, *(v4sf*)_ps_1);

  /* Evaluate the second polynom  (Pi/4 <= x <= 0) */

  y2 = *(v4sf*)_ps_sincof_p0;
  y2 = _mm_mul_ps(y2, z);
  y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p1);
  y2 = _mm_mul_ps(y2, z);
  y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p2);
  y2 = _mm_mul_ps(y2, z);
  y2 = _mm_mul_ps(y2, x);
  y2 = _mm_add_ps(y2, x);

  /* select the correct result from the two polynoms */
  xmm3 = poly_mask;
  y2 = _mm_and_ps(xmm3, y2); //, xmm3);
  y = _mm_andnot_ps(xmm3, y);
  y = _mm_add_ps(y,y2);
  /* update the sign */
  y = _mm_xor_ps(y, sign_bit);

  return y;
}

/*!
	since sin_ps and cos_ps are almost identical, sincos_ps could replace both of them..
   it is almost as fast, and gives you a free cosine with your sine
 */
static inline void sincos_ps(v4sf x, v4sf *s, v4sf *c)
{ v4sf xmm1, xmm2, sign_bit_sin, y, y2, z, swap_sign_bit_sin, poly_mask;
  v4sf sign_bit_cos;
#ifdef USE_SSE2
  v4si emm2;
#else
  v2si mm0, mm1, mm2, mm3, mm4, mm5;
#endif
  sign_bit_sin = x;
  /* take the absolute value */
  x = _mm_and_ps(x, *(v4sf*)_ps_inv_sign_mask);
  /* extract the sign bit (upper one) */
  sign_bit_sin = _mm_and_ps(sign_bit_sin, *(v4sf*)_ps_sign_mask);

  /* scale by 4/Pi */
  y = _mm_mul_ps(x, *(v4sf*)_ps_cephes_FOPI);

#ifdef USE_SSE2
  /* store the integer part of y in emm2 */
  emm2 = _mm_cvttps_epi32(y);

  /* j=(j+1) & (~1) (see the cephes sources) */
//   emm2 = _mm_add_epi32(emm2, *(v4si*)_pi32_1);
//   emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_inv1);
	emm2 = _mm_and_si128( _mm_add_epi32( _mm_cvttps_epi32(y), *(v4si*)_pi32_1 ), *(v4si*)_pi32_inv1 );
	y = _mm_cvtepi32_ps(emm2);

  /* get the swap sign flag for the sine */
//   emm0 = _mm_and_si128(emm2, *(v4si*)_pi32_4);
//   emm0 = _mm_slli_epi32(emm0, 29);
//   swap_sign_bit_sin = _mm_castsi128_ps(emm0);
	swap_sign_bit_sin = _mm_castsi128_ps( _mm_slli_epi32( _mm_and_si128(emm2, *(v4si*)_pi32_4), 29) );

  /* get the polynom selection mask for the sine*/
//   emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_2);
//   emm2 = _mm_cmpeq_epi32(emm2, _MM_SETZERO_SI128());
//   poly_mask = _mm_castsi128_ps(emm2);
	poly_mask = _mm_castsi128_ps( _mm_cmpeq_epi32( _mm_and_si128(emm2, *(v4si*)_pi32_2), _MM_SETZERO_SI128()) );
#else
  { v4sf xmm3 = _MM_SETZERO_PS();
	  /* store the integer part of y in mm2:mm3 */
	  xmm3 = _mm_movehl_ps(xmm3, y);
	  mm2 = _mm_cvttps_pi32(y);
	  mm3 = _mm_cvttps_pi32(xmm3);

	  /* j=(j+1) & (~1) (see the cephes sources) */
	  mm2 = _mm_add_pi32(mm2, *(v2si*)_pi32_1);
	  mm3 = _mm_add_pi32(mm3, *(v2si*)_pi32_1);
	  mm2 = _mm_and_si64(mm2, *(v2si*)_pi32_inv1);
	  mm3 = _mm_and_si64(mm3, *(v2si*)_pi32_inv1);

	  y = _mm_cvtpi32x2_ps(mm2, mm3);

	  mm4 = mm2;
	  mm5 = mm3;

	  /* get the swap sign flag for the sine */
	  mm0 = _mm_and_si64(mm2, *(v2si*)_pi32_4);
	  mm1 = _mm_and_si64(mm3, *(v2si*)_pi32_4);
	  mm0 = _mm_slli_pi32(mm0, 29);
	  mm1 = _mm_slli_pi32(mm1, 29);
	  COPY_MM_TO_XMM(mm0, mm1, swap_sign_bit_sin);

	  /* get the polynom selection mask for the sine */

	  mm2 = _mm_and_si64(mm2, *(v2si*)_pi32_2);
	  mm3 = _mm_and_si64(mm3, *(v2si*)_pi32_2);
	  mm2 = _mm_cmpeq_pi32(mm2, _MM_SETZERO_SI64());
	  mm3 = _mm_cmpeq_pi32(mm3, _MM_SETZERO_SI64());
	  COPY_MM_TO_XMM(mm2, mm3, poly_mask);
  }
#endif

  /* The magic pass: "Extended precision modular arithmetic"
     x = ((x - y * DP1) - y * DP2) - y * DP3; */
#ifdef __GNUC__
	x += y * ( *(v4sf*)_ps_minus_cephes_DP1 + *(v4sf*)_ps_minus_cephes_DP2 + *(v4sf*)_ps_minus_cephes_DP3 );
#else
//   xmm1 = *(v4sf*)_ps_minus_cephes_DP1;
//   xmm2 = *(v4sf*)_ps_minus_cephes_DP2;
//   xmm3 = *(v4sf*)_ps_minus_cephes_DP3;
//   xmm1 = _mm_mul_ps(y, xmm1);
//   xmm2 = _mm_mul_ps(y, xmm2);
//   xmm3 = _mm_mul_ps(y, xmm3);
//   x = _mm_add_ps(x, xmm1);
//   x = _mm_add_ps(x, xmm2);
//   x = _mm_add_ps(x, xmm3);
  	x = _mm_add_ps( x, _mm_mul_ps( y, _mm_add_ps( _mm_add_ps(*(v4sf*)_ps_minus_cephes_DP1, *(v4sf*)_ps_minus_cephes_DP2),
										*(v4sf*)_ps_minus_cephes_DP3 ) ) );
#endif

#ifdef USE_SSE2
//   emm4 = _mm_sub_epi32(emm4, *(v4si*)_pi32_2);
//   emm4 = _mm_andnot_si128(emm4, *(v4si*)_pi32_4);
//   emm4 = _mm_slli_epi32(emm4, 29);
//   sign_bit_cos = _mm_castsi128_ps(emm4);
	sign_bit_cos = _mm_castsi128_ps( _mm_slli_epi32( _mm_andnot_si128( _mm_sub_epi32(emm2, *(v4si*)_pi32_2), *(v4si*)_pi32_4), 29) );
#else
  /* get the sign flag for the cosine */
  mm4 = _mm_sub_pi32(mm4, *(v2si*)_pi32_2);
  mm5 = _mm_sub_pi32(mm5, *(v2si*)_pi32_2);
  mm4 = _mm_andnot_si64(mm4, *(v2si*)_pi32_4);
  mm5 = _mm_andnot_si64(mm5, *(v2si*)_pi32_4);
  mm4 = _mm_slli_pi32(mm4, 29);
  mm5 = _mm_slli_pi32(mm5, 29);
  COPY_MM_TO_XMM(mm4, mm5, sign_bit_cos);
  _mm_empty(); /* good-bye mmx */
#endif

  sign_bit_sin = _mm_xor_ps(sign_bit_sin, swap_sign_bit_sin);


  /* Evaluate the first polynom  (0 <= x <= Pi/4) */
#ifdef __GNUC__
  z = x * x;
  y = ( ( ( (*(v4sf*)_ps_coscof_p0) * z + *(v4sf*)_ps_coscof_p1 ) * z + *(v4sf*)_ps_coscof_p2 ) * z
  		- *(v4sf*)_ps_0p5 ) * z + *(v4sf*)_ps_1;
#else
  z = _mm_mul_ps(x,x);
//   y = *(v4sf*)_ps_coscof_p0;
//
//   y = _mm_mul_ps(y, z);
//   y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p1);
//   y = _mm_mul_ps(y, z);
//   y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p2);
//   y = _mm_mul_ps(y, z);
//   y = _mm_mul_ps(y, z);
//   tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
//   y = _mm_sub_ps(y, tmp);
//   y = _mm_add_ps(y, *(v4sf*)_ps_1);
	y = _mm_add_ps(
		_mm_mul_ps(
			_mm_sub_ps(
				_mm_mul_ps(
					_mm_add_ps(
						_mm_mul_ps(
							_mm_add_ps(
								_mm_mul_ps(*(v4sf*)_ps_coscof_p0, z),
								*(v4sf*)_ps_coscof_p1 ),
							z ),
						*(v4sf*)_ps_coscof_p2 ),
					z ),
				*(v4sf*)_ps_0p5 ),
			z ),
		*(v4sf*)_ps_1 );
#endif

  /* Evaluate the second polynom  (Pi/4 <= x <= 0) */

#ifdef __GNUC__
	y2 = ( ( ( ( ((*(v4sf*)_ps_sincof_p0) * z ) + *(v4sf*)_ps_sincof_p1 ) * z ) + *(v4sf*)_ps_sincof_p2 ) * z
		+ *(v4sf*)_ps_1 ) * x;
#else
//   y2 = *(v4sf*)_ps_sincof_p0;
//   y2 = _mm_mul_ps(y2, z);
//   y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p1);
//   y2 = _mm_mul_ps(y2, z);
//   y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p2);
//   y2 = _mm_mul_ps(y2, z);
//   y2 = _mm_mul_ps(y2, x);
//   y2 = _mm_add_ps(y2, x);
	y2 = _mm_mul_ps(
		_mm_add_ps(
			_mm_mul_ps(
				_mm_add_ps(
					_mm_mul_ps(
						_mm_add_ps(
							_mm_mul_ps(*(v4sf*)_ps_sincof_p0, z ),
							*(v4sf*)_ps_sincof_p1 ),
						z ),
					*(v4sf*)_ps_sincof_p2 ),
				z ),
			*(v4sf*)_ps_1 ),
		x );
#endif

  /* select the correct result from the two polynoms */
  {
#if defined(__GNUC__0) && !defined(__MINGW32__)
	  // less precise results
	  xmm1 = _mm_andnot_ps( poly_mask, y) + (poly_mask & y2);
	  xmm2 = y + y2 - xmm1;
	  /* update the sign */
	  *s = xmm1 | sign_bit_sin;
	  *c = xmm2 | sign_bit_cos;
#else
// 	  v4sf ysin2 = _mm_and_ps( poly_mask, y2);
// 	  v4sf ysin1 = _mm_andnot_ps( poly_mask, y);
// 	  y2 = _mm_sub_ps(y2,ysin2);
// 	  y = _mm_sub_ps(y, ysin1);
//
// 	  xmm1 = _mm_add_ps(ysin1,ysin2);
// 	  xmm2 = _mm_add_ps(y,y2);
	  xmm1 = _mm_add_ps( _mm_andnot_ps( poly_mask, y), _mm_and_ps(poly_mask, y2) );
	  xmm2 = _mm_sub_ps( _mm_add_ps( y, y2 ), xmm1 );
	  /* update the sign */
	  *s = _mm_xor_ps(xmm1, sign_bit_sin);
	  *c = _mm_xor_ps(xmm2, sign_bit_cos);
#endif
  }

}

#ifdef USE_SSE2
/*!
	computes sine and cosine of the 2 doubles in x
 */
static inline void sincos_pd(v2df x, v2df *s, v2df *c)
{ v2df xmm1, xmm2, sign_bit_sin, y, y2, z, swap_sign_bit_sin, poly_mask;
  v2df sign_bit_cos;
  v4si emm2;
	sign_bit_sin = x;
	/* take the absolute value */
	x = _mm_and_pd(x, *(v2df*)_pd_inv_sign_mask);
	/* extract the sign bit (upper one) */
	sign_bit_sin = _mm_and_pd(sign_bit_sin, *(v2df*)_pd_sign_mask);

	/* scale by 4/Pi */
	y = _mm_mul_pd(x, *(v2df*)_pd_cephes_FOPI);

	/* store the integer part of y in emm2 */
	emm2 = _mm_cvttpd_epi32(y);

	/* j=(j+1) & (~1) (see the cephes sources) */
	emm2 = _mm_and_si128( _mm_add_epi64( _mm_cvttpd_epi32(y), *(v4si*)_pi32_1 ), *(v4si*)_pi32_inv1 );
	y = _mm_cvtepi32_pd(emm2);

	/* get the swap sign flag for the sine */
	{ v4sf sss = _mm_castsi128_ps( _mm_slli_epi32( _mm_and_si128(emm2, *(v4si*)_pi32_4), 29) );
	  float *fsss = ((float*)&sss);
		swap_sign_bit_sin = _MM_SETR_PD( fsss[0], fsss[1] );
	}

	/* get the polynom selection mask for the sine*/
	{ v4sf pm = _mm_castsi128_ps( _mm_cmpeq_epi32( _mm_and_si128(emm2, *(v4si*)_pi32_2), _MM_SETZERO_SI128()) );
	  float *fpm = ((float*)&pm);
		poly_mask = _MM_SETR_PD( fpm[0], fpm[1] );
	}

	/* The magic pass: "Extended precision modular arithmetic"
	 x = ((x - y * DP1) - y * DP2) - y * DP3; */
#ifdef __GNUC__
	x += y * ( *(v2df*)_pd_minus_cephes_DP1 + *(v2df*)_pd_minus_cephes_DP2 + *(v2df*)_pd_minus_cephes_DP3 );
#else
  	x = _mm_add_pd( x, _mm_mul_pd( y, _mm_add_pd( _mm_add_pd(*(v2df*)_pd_minus_cephes_DP1, *(v2df*)_pd_minus_cephes_DP2),
										*(v2df*)_pd_minus_cephes_DP3 ) ) );
#endif

	{ v4sf sbc = _mm_castsi128_ps( _mm_slli_epi32( _mm_andnot_si128( _mm_sub_epi32(emm2, *(v4si*)_pi32_2), *(v4si*)_pi32_4), 29) );
	  float *fsbc = ((float*)&sbc);
		sign_bit_cos = _MM_SETR_PD( fsbc[0], fsbc[1] );
	}

	sign_bit_sin = _mm_xor_pd(sign_bit_sin, swap_sign_bit_sin);


	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
#ifdef __GNUC__
	z = x * x;
	y = ( ( ( (*(v2df*)_pd_coscof_p0) * z + *(v2df*)_pd_coscof_p1 ) * z + *(v2df*)_pd_coscof_p2 ) * z
  		- *(v2df*)_pd_0p5 ) * z + *(v2df*)_pd_1;
#else
	z = _mm_mul_pd(x,x);
	y = _mm_add_pd(
			_mm_mul_pd(
				 _mm_sub_pd(
					  _mm_mul_pd(
						   _mm_add_pd(
							    _mm_mul_pd(
									_mm_add_pd(
											 _mm_mul_pd(*(v2df*)_pd_coscof_p0, z),
											 *(v2df*)_pd_coscof_p1 ),
									z ),
							    *(v2df*)_pd_coscof_p2 ),
						   z ),
					  *(v2df*)_pd_0p5 ),
				 z ),
			*(v2df*)_pd_1 );
#endif

	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */

#ifdef __GNUC__
	y2 = ( ( ( ( ((*(v2df*)_pd_sincof_p0) * z ) + *(v2df*)_pd_sincof_p1 ) * z ) + *(v2df*)_pd_sincof_p2 ) * z
		 + *(v2df*)_pd_1 ) * x;
#else
	y2 = _mm_mul_pd(
			 _mm_add_pd(
				  _mm_mul_pd(
					   _mm_add_pd(
						    _mm_mul_pd(
								_mm_add_pd(
										 _mm_mul_pd(*(v2df*)_pd_sincof_p0, z ),
										 *(v2df*)_pd_sincof_p1 ),
								z ),
						    *(v2df*)_pd_sincof_p2 ),
					   z ),
				  *(v2df*)_pd_1 ),
			 x );
#endif

	/* select the correct result from the two polynoms */
	{
#if defined(__GNUC__0) && !defined(__MINGW32__)
		xmm1 = _mm_andnot_pd( poly_mask, y) + (poly_mask & y2);
		xmm2 = y + y2 - xmm1;
		/* update the sign */
		*s = xmm1 | sign_bit_sin;
		*c = xmm2 | sign_bit_cos;
#else
		xmm1 = _mm_add_pd( _mm_andnot_pd( poly_mask, y), _mm_and_pd(poly_mask, y2) );
		xmm2 = _mm_sub_pd( _mm_add_pd( y, y2 ), xmm1 );
		/* update the sign */
		*s = _mm_xor_pd(xmm1, sign_bit_sin);
		*c = _mm_xor_pd(xmm2, sign_bit_cos);
#endif
	}

}
#endif

#ifdef USE_SSE2

/*!
	computes the cumulative sum of the double array xa[n] using SSE2 intrinsics
 */
// static inline double CumSum( double *xa, int n )
// { __m128d vsum;
//   register int i, N_4 = n-4+1;
//   register double sum = 0;
// 	for( i = 0 ; i < N_4 ; i+=4, xa+=4 ){
// #ifdef __GNUC__
// 		vsum = *((__m128d*)&xa[2]) + *((__m128d*)xa);
// #else
// 		vsum = _mm_add_pd( *((__m128d*)&xa[2]), *((__m128d*)xa) );
// #endif
// 		sum += *((double*)&vsum) + ((double*)&vsum)[1];
// 	}
// 	for( ; i < n ; i++ ){
// 		sum += *xa++;
// 	}
// 	return sum;
// }
static inline double CumSum(double *xa, int N)
{ double sum;
	if( xa && N > 0 ){
	  v2df *va = (v2df*) xa, vsum = _MM_SETZERO_PD();
	  int i, N_4 = N-4+1;
		for( i = 0 ; i < N_4 ; va+=2 ){
			vsum = _mm_add_pd( vsum, _mm_add_pd( va[0], va[1] ) );
			i += 4;
		}
		sum = VELEM(double,vsum,0) + VELEM(double,vsum,1);
		for( ; i < N; i++ ){
			sum += xa[i];
		}
	}
	else{
		sum = 0.0;
	}
	return sum;
}


/*!
	computes the cumulative sum of the squares of the values in double array xa[n] using SSE2 intrinsics
 */
static inline double CumSumSq( double *xa, int n )
{ __m128d vsumsq;
  register int i, N_4 = n-4+1;
  register double sumsq = 0;
	for( i = 0 ; i < N_4 ; i+=4, xa+=4 ){
#ifdef __GNUC__
		vsumsq = *((__m128d*)&xa[2]) * *((__m128d*)&xa[2]) + *((__m128d*)xa) * *((__m128d*)xa);
#else
		vsumsq = _mm_add_pd( _mm_mul_pd( *((__m128d*)&xa[2]), *((__m128d*)&xa[2]) ),
					   _mm_mul_pd( *((__m128d*)xa), *((__m128d*)xa) ) );
#endif
		sumsq += *((double*)&vsumsq) + ((double*)&vsumsq)[1];
	}
	for( ; i < n ; i++, xa++ ){
		sumsq += *xa * *xa;
	}
	return sumsq;
}

/*!
	computes the cumulative sum of the values and their squares in double array xa[n] using SSE2 intrinsics
 */
static inline double CumSumSumSq( double *xa, int n, double *sumSQ )
{ __m128d vsum, vsumsq;
  register int i, N_4 = n-4+1;
  register double sum = 0.0, sumsq = 0;
	for( i = 0 ; i < N_4 ; i+=4, xa+=4 ){
#ifdef __GNUC__
		vsum = *((__m128d*)&xa[2]) + *((__m128d*)xa);
		vsumsq = *((__m128d*)&xa[2]) * *((__m128d*)&xa[2]) + *((__m128d*)xa) * *((__m128d*)xa);
#else
		vsum = _mm_add_pd( *((__m128d*)&xa[2]), *((__m128d*)xa) );
		vsumsq = _mm_add_pd( _mm_mul_pd( *((__m128d*)&xa[2]), *((__m128d*)&xa[2]) ),
					   _mm_mul_pd( *((__m128d*)xa), *((__m128d*)xa) ) );
#endif
		sum += *((double*)&vsum) + ((double*)&vsum)[1];
		sumsq += *((double*)&vsumsq) + ((double*)&vsumsq)[1];
	}
	for( ; i < n ; i++, xa++ ){
		sum += *xa;
		sumsq += *xa * *xa;
	}
	*sumSQ = sumsq;
	return sum;
}

/*!
	scalar version of CumSum without explicit SSE2 intrinsics
 */
static inline double scalCumSum( double *xa, int n )
{ register int i;
  register double sum = 0.0;
	for( i = 0 ; i < n ; i++ ){
		sum += *xa++;
	}
	return sum;
}

/*!
	scalar version of CumSumSq without explicit SSE2 intrinsics
 */
static inline double scalCumSumSq( double *xa, int n )
{ register int i;
  register double sumsq = 0.0;
	for( i = 0 ; i < n ; i++, xa++ ){
		sumsq += *xa * *xa;
	}
	return sumsq;
}

/*!
	scalar version of CumSumSumSq without explicit SSE2 intrinsics
 */
static inline double scalCumSumSumSq( double *xa, int n, double *sumSQ )
{ register int i;
  register double sum = 0.0, sumsq = 0.0;
	for( i = 0 ; i < n ; i++, xa++ ){
		sum += *xa;
		sumsq += *xa * *xa;
	}
	*sumSQ = sumsq;
	return sum;
}

/*!
	computes the cumulative product of the double array xa[n] using SSE2 intrinsics
 */
static inline double CumMul(double *xa, int N)
{ double cum;
	if( xa && N > 0 ){
	  v2df *va = (v2df*) xa, vcum = _MM_SET1_PD(1.0);
	  int i, N_4 = N-4+1;
		for( i = 0 ; i < N_4 ; va+=2 ){
			vcum = _mm_mul_pd( vcum, _mm_mul_pd( va[0], va[1] ) );
			i += 4;
		}
		cum = VELEM(double,vcum,0) * VELEM(double,vcum,1);
		for( ; i < N; i++ ){
			cum *= xa[i];
		}
	}
	else{
		cum = 0.0;
	}
	return cum;
}

#else

/*!
	computes the cumulative sum of the double array xa[n] using traditional scalar code
 */
static inline double CumSum( double *xa, int n )
{ register int i;
  register double sum = 0.0;
	for( i = 0 ; i < n ; i++ ){
		sum += *xa++;
	}
	return sum;
}

/*!
	alternative for CumSum
 */
static inline double scalCumSum( double *xa, int n )
{
	return CumSum(xa,n);
}

/*!
	computes the cumulative sum of the squares of the values in double array xa[n] using traditional scalar code
 */
static inline double CumSumSq( double *xa, int n )
{ register int i;
  register double sumsq = 0.0;
	for( i = 0 ; i < n ; i++, xa++ ){
		sumsq += *xa * *xa;
	}
	return sumsq;
}

/*!
	alternative for CumSumSq
 */
static inline double scalCumSumSq( double *xa, int n )
{
	return CumSumSq(xa,n);
}

/*!
	computes the cumulative sum of the values and their squares in double array xa[n] using traditional scalar code
 */
static inline double CumSumSumSq( double *xa, int n, double *sumSQ )
{ register int i;
  register double sum = 0.0, sumsq = 0.0;
	for( i = 0 ; i < n ; i++, xa++ ){
		sum += *xa;
		sumsq += *xa * *xa;
	}
	*sumSQ = sumsq;
	return sum;
}

/*!
	alternative for CumSumSumSq
 */
static inline double scalCumSumSumSq( double *xa, int n, double *sumSQ )
{
	return CumSumSumSq(xa,n,sumSQ);
}

#endif //USE_SSE2

#endif // SSE_MATHFUN_WITH_CODE

//// Some SSE "extensions", and equivalents not using SSE explicitly:

#ifdef USE_SSE2

#	if defined(__x86_64__) || defined(x86_64) || defined(_LP64)
//  	static inline v2df _mm_abs_pd( v2df a )
//  	{ _PD_CONST_TYPE(abs_mask, long long, ~0x8000000000000000LL);
//  		return _mm_and_pd(a, *(v2df*)_pd_abs_mask);
//  	}
	/*!
		SSE2 'intrinsic' to take the absolute value of a
	 */
	static inline v2df _mm_abs_pd( register v2df a )
	{ const static long long am1[2] = {~0x8000000000000000LL,~0x8000000000000000LL};
		return _mm_and_pd(a, *((v2df*)am1) );
	}
	//static inline double _mm_abs_sd( double a )
	//{ const static long long am2 = {~0x8000000000000000LL};
	//  v2si r = _mm_and_si64( *((v2si*)&a), *((v2si*)&am2) );
	//	return *((double*) &r);
	//}
#	else
	// no native support for 64bit ints: don't lose time on that!
	/*!
		SSE2 'intrinsic' to take the absolute value of a
	 */
 	static inline v2df _mm_abs_pd( register v2df a )
 	{ const v4si am1 = _mm_set_epi32(0x7fffffff,0xffffffff,0x7fffffff,0xffffffff);
 		return _mm_and_pd(a, *((v2df*)&am1) );
 	}
	//static inline double _mm_abs_sd( double a )
	//{ const static unsigned long long am2 = 0x7fffffffffffffffLL;
	//  const v4si am1 = _mm_set_epi32(0x7fffffff,0xffffffff,0x7fffffff,0xffffffff);
	//  v2si r = _mm_and_si64( *((v2si*)&a), *((v2si*)&am1) );
	//	_mm_empty();
	//	return *((double*)&r);
//	  union { double d; v2si r; } ret;
//		ret.r = _mm_and_si64( *((v2si*)&a), *((v2si*)&am1) );
//		a = ret.d;
//		return a;
	//}
#	endif // i386 or x86_64
 	static inline v4sf _mm_abs_ps( register v4sf a )
 	{ const v4si am1 = _mm_set_epi32(0x7fffffff,0x7fffffff,0x7fffffff,0x7fffffff);
 		return _mm_and_ps(a, *((v4sf*)&am1) );
 	}

/*!
	clip a value to a min/max range
 */
static inline v2df _mm_clip_pd( v2df val, v2df valMin, v2df valMax )
{
	return _mm_max_pd( _mm_min_pd( val, valMax ), valMin );
}

/*!
	return an SSE2 vector of 2 doubles initialised with val0 and val1, clipped to
	the specified range
 */
static inline v2df _mm_setr_clipped_pd( double val0, double val1, v2df valMin, v2df valMax )
{
	return _mm_clip_pd( _MM_SETR_PD(val0,val1), valMin, valMax );
}
#endif // USE_SSE2
#ifdef USE_SSE4
	static inline double ssceil(double a)
	{ v2df va = _mm_ceil_pd( _MM_SETR_PD(a,0) );
#	if !defined(__x86_64__) && !defined(x86_64) && !defined(_LP64)
		_mm_empty();
#	endif
		return *((double*)&va);
	}

	static inline double ssfloor(double a)
	{ v2df va = _mm_floor_pd( _MM_SETR_PD(a,0) );
#	if !defined(__x86_64__) && !defined(x86_64) && !defined(_LP64)
		_mm_empty();
#	endif
		return *((double*)&va);
	}
	static inline double ssround( double a )
	{ v2df va = _mm_round_pd( _MM_SETR_PD(a,0), _MM_FROUND_TO_NEAREST_INT|_MM_FROUND_NO_EXC);
#	if !defined(__x86_64__) && !defined(x86_64) && !defined(_LP64)
		_mm_empty();
#	endif
		return *((double*)&va);
	}
#else
	static inline double ssceil(double a)
	{
		return ceil(a);
	}
	static inline double ssfloor(double a)
	{
		return floor(a);
	}
	static inline double ssround( double a )
	{
		return (a >= 0)? floor( a + 0.5 ) : -ceil( -a - 0.5 );
	}
#endif //USE_SSE4


// SSE-like convenience functions (note the absence of a leading _!)

/*!
	return an SSE2 vector of 2 doubles initialised with val0 and val1, clipped to
	the specified range. Does not use SSE2 intrinsics.
 */
static inline v2df *mm_setr_clipped_pd( v2df *val, double val0, double val1, v2df *valMin,  v2df *valMax )
{
	if( val0 > ((double*)valMax)[0] ){
		((double*)val)[0] = ((double*)valMax)[0];
	}
	else if( val0 < ((double*)valMin)[0] ){
		((double*)val)[0] = ((double*)valMin)[0];
	}
	else{
		((double*)val)[0] = val0;
	}
	if( val1 > ((double*)valMax)[1] ){
		((double*)val)[1] = ((double*)valMax)[1];
	}
	else if( val1 < ((double*)valMin)[1] ){
		((double*)val)[1] = ((double*)valMin)[1];
	}
	else{
		((double*)val)[1] = val1;
	}
	return val;
}

/*!
	SSE2 'intrinsic' to take the absolute value of a. Doesn't use SSE2 intrinsics
 */
static inline v2df *mm_clip_pd( v2df *val, v2df *valMin,  v2df *valMax )
{
	if( ((double*)val)[0] > ((double*)valMax)[0] ){
		((double*)val)[0] = ((double*)valMax)[0];
	}
	else if( ((double*)val)[0] < ((double*)valMin)[0] ){
		((double*)val)[0] = ((double*)valMin)[0];
	}
	if( ((double*)val)[1] > ((double*)valMax)[1] ){
		((double*)val)[1] = ((double*)valMax)[1];
	}
	else if( ((double*)val)[1] < ((double*)valMin)[1] ){
		((double*)val)[1] = ((double*)valMin)[1];
	}
	return val;
}

/*!
	emulation of the _mm_add_pd SSE2 intrinsic
 */
static inline v2df *mm_add_pd( v2df *c, v2df *a, v2df *b )
{
	((double*)c)[0] = ((double*)a)[0] + ((double*)b)[0];
	((double*)c)[1] = ((double*)a)[1] + ((double*)b)[1];
	return c;
}

/*!
	emulation of the _mm_add_pd SSE2 intrinsic
 */
static inline v2df *mm_sub_pd( v2df *c, v2df *a, v2df *b )
{
	((double*)c)[0] = ((double*)a)[0] - ((double*)b)[0];
	((double*)c)[1] = ((double*)a)[1] - ((double*)b)[1];
	return c;
}

/*!
	emulation of the _mm_sub_pd SSE2 intrinsic
 */
static inline v2df *mm_div_pd( v2df *c, v2df *a, v2df *b )
{
	((double*)c)[0] = ((double*)a)[0] / ((double*)b)[0];
	((double*)c)[1] = ((double*)a)[1] / ((double*)b)[1];
	return c;
}

/*!
	emulation of the _mm_mul_pd SSE2 intrinsic
 */
static inline v2df *mm_mul_pd( v2df *c, v2df *a, v2df *b )
{
	((double*)c)[0] = ((double*)a)[0] * ((double*)b)[0];
	((double*)c)[1] = ((double*)a)[1] * ((double*)b)[1];
	return c;
}

/*!
	non SSE emulation of the _mm_abs_pd 'intrinsic' defined elsewhere in this file
 */
static inline v2df *mm_abs_pd( v2df *val, v2df *a )
{
	((double*)val)[0] = (((double*)a)[0] >= 0)? ((double*)a)[0] : -((double*)a)[0];
	((double*)val)[1] = (((double*)a)[1] >= 1)? ((double*)a)[1] : -((double*)a)[1];
	return val;
}

/*!
	emulation of the _mm_round_pd SSE4 intrinsic.
	@n
	NB: the SSE4 intrinsic is at least twice as fast as the non-SSE calculation, PER value
	so it pays to replace round(x) with _mm_round_pd(_mm_setr_pd(x)) - idem for floor and ceil
 */
static inline v2df *mm_round_pd( v2df *val, v2df *a )
{
	((double*)val)[0] = (((double*)a)[0] >= 0)? floor( ((double*)a)[0] + 0.5 ) : -ceil( -((double*)a)[0] - 0.5 );
	((double*)val)[1] = (((double*)a)[1] >= 0)? floor( ((double*)a)[1] + 0.5 ) : -ceil( -((double*)a)[1] - 0.5 );
	return val;
}

#define _SSE_MATHFUN_H
#endif
