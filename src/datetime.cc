// MIT License
//
// Copyright (c) 2021 kevin lau
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "datetime.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "fmt/format.h"

// TODO: 溢出检查，部分函数没有进行溢出检查

namespace datetime {

static constexpr long us_per_us = 1L;
static constexpr long us_per_ms = 1000L;
static constexpr long us_per_second = us_per_ms * 1000L;
static constexpr long us_per_minute = us_per_second * 60L;
static constexpr long us_per_hour = us_per_minute * 60L;
static constexpr long us_per_day = us_per_hour * 24L;
static constexpr long us_per_week = us_per_day * 7L;
static constexpr long seconds_per_day = 3600L * 24L;

/* k = i+j overflows iff k differs in sign from both inputs,
 * iff k^i has sign bit set and k^j has sign bit set,
 * iff (k^i)&(k^j) has sign bit set.
 */
template <class SignedType>
static inline bool signed_add_overflow(SignedType i, SignedType j) {
  SignedType result = i + j;
  return ((result ^ i) & (result ^ j)) < 0;
}

/* Compute Python divmod(x, y), returning the quotient and storing the
 * remainder into *r.  The quotient is the floor of x/y, and that's
 * the real point of this.  C will probably truncate instead (C99
 * requires truncation; C89 left it implementation-defined).
 * Simplification:  we *require* that y > 0 here.  That's appropriate
 * for all the uses made of it.  This simplifies the code and makes
 * the overflow case impossible (divmod(LONG_MIN, -1) is the only
 * overflow case).
 */
template <class IntType>
static long divmod(IntType x, IntType y, IntType *r) {
  IntType quo;

  assert(y > 0);
  quo = x / y;
  *r = x - quo * y;
  if (*r < 0) {
    --quo;
    *r += y;
  }
  assert(0 <= *r && *r < y);
  return quo;
}

template <class IntType>
static long div(IntType x, IntType y) {
  IntType quo;
  IntType r;

  assert(y > 0);
  quo = x / y;
  r = x - quo * y;
  if (r < 0) {
    --quo;
  }
  return quo;
}

template <class IntType>
static long mod(IntType x, IntType y) {
  IntType quo;
  IntType r;

  assert(y > 0);
  quo = x / y;
  r = x - quo * y;
  if (r < 0) {
    r += y;
  }
  assert(0 <= r && r < y);
  return r;
}

/* Round a double to the nearest long.  |x| must be small enough to fit
 * in a C long; this is not checked.
 */
[[gnu::unused]] static long round_to_long(double x) {
  if (x >= 0.0)
    x = floor(x + 0.5);
  else
    x = ceil(x - 0.5);
  return (long)x;
}

/* For each month ordinal in 1..12, the number of days in that month,
 * and the number of days before that month in the same year.  These
 * are correct for non-leap years only.
 */
static const int _days_in_month[] = {
    0, /* unused; this vector uses 1-based indexing */
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static const int _days_before_month[] = {
    0, /* unused; this vector uses 1-based indexing */
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

/* year -> 1 if leap year, else 0. */
static inline int is_leap(int year) {
  /* Cast year to unsigned.  The result is the same either way, but
   * C can generate faster code for unsigned mod than for signed
   * mod (especially for % 4 -- a good compiler should just grab
   * the last 2 bits when the LHS is unsigned).
   */
  const unsigned int uyear = static_cast<unsigned int>(year);
  return uyear % 4 == 0 && (uyear % 100 != 0 || uyear % 400 == 0);
}

/* year, month -> number of days in that month in that year */
static inline int days_in_month(int year, int month) {
  assert(month >= 1);
  assert(month <= 12);
  if (month == 2 && is_leap(year)) {
    return 29;
  } else {
    return _days_in_month[month];
  }
}

/* year, month -> number of days in year preceding first day of month */
static inline int days_before_month(int year, int month) {
  int days;

  assert(month >= 1);
  assert(month <= 12);
  days = _days_before_month[month];
  if (month > 2 && is_leap(year)) {
    ++days;
  }
  return days;
}

/* year -> number of days before January 1st of year.  Remember that we
 * start with year 1, so days_before_year(1) == 0.
 */
static inline int days_before_year(int year) {
  int y = year - 1;
  /* This is incorrect if year <= 0; we really want the floor
   * here.  But so long as MINYEAR is 1, the smallest year this
   * can see is 1.
   */
  assert(year >= 1);
  return y * 365 + y / 4 - y / 100 + y / 400;
}

/* Number of days in 4, 100, and 400 year cycles.  That these have
 * the correct values is asserted in the module init function.
 */
#define DI4Y 1461     /* days_before_year(5); days in 4 years */
#define DI100Y 36524  /* days_before_year(101); days in 100 years */
#define DI400Y 146097 /* days_before_year(401); days in 400 years  */

#define MINYEAR 1
#define MAXYEAR 9999
#define MAXORDINAL 3652059 /* date(9999,12,31).toordinal() */

/* Nine decimal digits is easy to communicate, and leaves enough room
 * so that two delta days can be added w/o fear of overflowing a signed
 * 32-bit int, and with plenty of room left over to absorb any possible
 * carries from adding seconds.
 */
#define MAX_DELTA_DAYS 999999999

/* ordinal -> year, month, day, considering 01-Jan-0001 as day 1. */
static void ord_to_ymd(int ordinal, int *year, int *month, int *day) {
  int n, n1, n4, n100, n400, leapyear, preceding;

  /* ordinal is a 1-based index, starting at 1-Jan-1.  The pattern of
   * leap years repeats exactly every 400 years.  The basic strategy is
   * to find the closest 400-year boundary at or before ordinal, then
   * work with the offset from that boundary to ordinal.  Life is much
   * clearer if we subtract 1 from ordinal first -- then the values
   * of ordinal at 400-year boundaries are exactly those divisible
   * by DI400Y:
   *
   *    D  M   Y            n              n-1
   *    -- --- ----        ----------     ----------------
   *    31 Dec -400        -DI400Y       -DI400Y -1
   *     1 Jan -399         -DI400Y +1   -DI400Y      400-year boundary
   *    ...
   *    30 Dec  000        -1             -2
   *    31 Dec  000         0             -1
   *     1 Jan  001         1              0          400-year boundary
   *     2 Jan  001         2              1
   *     3 Jan  001         3              2
   *    ...
   *    31 Dec  400         DI400Y        DI400Y -1
   *     1 Jan  401         DI400Y +1     DI400Y      400-year boundary
   */
  assert(ordinal >= 1);
  --ordinal;
  n400 = ordinal / DI400Y;
  n = ordinal % DI400Y;
  *year = n400 * 400 + 1;

  /* Now n is the (non-negative) offset, in days, from January 1 of
   * year, to the desired date.  Now compute how many 100-year cycles
   * precede n.
   * Note that it's possible for n100 to equal 4!  In that case 4 full
   * 100-year cycles precede the desired day, which implies the
   * desired day is December 31 at the end of a 400-year cycle.
   */
  n100 = n / DI100Y;
  n = n % DI100Y;

  /* Now compute how many 4-year cycles precede it. */
  n4 = n / DI4Y;
  n = n % DI4Y;

  /* And now how many single years.  Again n1 can be 4, and again
   * meaning that the desired day is December 31 at the end of the
   * 4-year cycle.
   */
  n1 = n / 365;
  n = n % 365;

  *year += n100 * 100 + n4 * 4 + n1;
  if (n1 == 4 || n100 == 4) {
    assert(n == 0);
    *year -= 1;
    *month = 12;
    *day = 31;
    return;
  }

  /* Now the year is correct, and n is the offset from January 1.  We
   * find the month via an estimate that's either exact or one too
   * large.
   */
  leapyear = n1 == 3 && (n4 != 24 || n100 == 3);
  assert(leapyear == is_leap(*year));
  *month = (n + 50) >> 5;
  preceding = (_days_before_month[*month] + (*month > 2 && leapyear));
  if (preceding > n) {
    /* estimate is too large */
    *month -= 1;
    preceding -= days_in_month(*year, *month);
  }
  n -= preceding;
  assert(0 <= n);
  assert(n < days_in_month(*year, *month));

  *day = n + 1;
}

/* year, month, day -> ordinal, considering 01-Jan-0001 as day 1. */
static inline int ymd_to_ord(int year, int month, int day) {
  return days_before_year(year) + days_before_month(year, month) + day;
}

/* Day of week, where Monday==0, ..., Sunday==6.  1/1/1 was a Monday. */
[[gnu::unused]] static inline int weekday(int year, int month, int day) {
  return (ymd_to_ord(year, month, day) + 6) % 7;
}

/* Ordinal of the Monday starting week 1 of the ISO year.  Week 1 is the
 * first calendar week containing a Thursday.
 */
[[gnu::unused]] static int iso_week1_monday(int year) {
  int first_day = ymd_to_ord(year, 1, 1); /* ord of 1/1 */
  /* 0 if 1/1 is a Monday, 1 if a Tue, etc. */
  int first_weekday = (first_day + 6) % 7;
  /* ordinal of closest Monday at or before 1/1 */
  int week1_monday = first_day - first_weekday;

  if (first_weekday > 3) /* if 1/1 was Fri, Sat, Sun */
    week1_monday += 7;
  return week1_monday;
}

/* ---------------------------------------------------------------------------
 * Range checkers.
 */

/* Check that -MAX_DELTA_DAYS <= days <= MAX_DELTA_DAYS.  If so, return 0.
 * If not, raise OverflowError and return -1.
 */
static int check_delta_day_range(int days) {
  if (-MAX_DELTA_DAYS <= days && days <= MAX_DELTA_DAYS) {
    return 0;
  }
  return -1;
}

/* Check that date arguments are in range.  Return 0 if they are.  If they
 * aren't, raise ValueError and return -1.
 */
int check_date_args(int year, int month, int day) {
  if (year < MINYEAR || year > MAXYEAR) {
    return -1;
  }
  if (month < 1 || month > 12) {
    return -1;
  }
  if (day < 1 || day > days_in_month(year, month)) {
    return -1;
  }
  return 0;
}

/* Check that time arguments are in range.  Return 0 if they are.  If they
 * aren't, raise ValueError and return -1.
 */
static int check_time_args(int h, int m, int s, int us) {
  if (h < 0 || h > 23) {
    return -1;
  }
  if (m < 0 || m > 59) {
    return -1;
  }
  if (s < 0 || s > 59) {
    return -1;
  }
  if (us < 0 || us > 999999) {
    return -1;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Normalization utilities.
 */

/* One step of a mixed-radix conversion.  A "hi" unit is equivalent to
 * factor "lo" units.  factor must be > 0.  If *lo is less than 0, or
 * at least factor, enough of *lo is converted into "hi" units so that
 * 0 <= *lo < factor.  The input values must be such that int overflow
 * is impossible.
 */
static void normalize_pair(int *hi, int *lo, int factor) {
  assert(factor > 0);
  assert(lo != hi);
  if (*lo < 0 || *lo >= factor) {
    const int num_hi = divmod(*lo, factor, lo);
    const int new_hi = *hi + num_hi;
    assert(!signed_add_overflow(new_hi, *hi, num_hi));
    *hi = new_hi;
  }
  assert(0 <= *lo && *lo < factor);
}

/* Fiddle days (d), seconds (s), and microseconds (us) so that
 *      0 <= *s < 24*3600
 *      0 <= *us < 1000000
 * The input values must be such that the internals don't overflow.
 * The way this routine is used, we don't get close.
 */
static void normalize_d_s_us(int *d, int *s, int *us) {
  if (*us < 0 || *us >= 1000000) {
    normalize_pair(s, us, 1000000);
    /* |s| can't be bigger than about
     * |original s| + |original us|/1000000 now.
     */
  }
  if (*s < 0 || *s >= 24 * 3600) {
    normalize_pair(d, s, 24 * 3600);
    /* |d| can't be bigger than about
     * |original d| +
     * (|original s| + |original us|/1000000) / (24*3600) now.
     */
  }
  assert(0 <= *s && *s < 24 * 3600);
  assert(0 <= *us && *us < 1000000);
}

/* Fiddle years (y), months (m), and days (d) so that
 *      1 <= *m <= 12
 *      1 <= *d <= days_in_month(*y, *m)
 * The input values must be such that the internals don't overflow.
 * The way this routine is used, we don't get close.
 */
static int normalize_y_m_d(int *y, int *m, int *d) {
  int dim; /* # of days in month */

  /* In actual use, m is always the month component extracted from a
   * date/datetime object.  Therefore it is always in [1, 12] range.
   */

  assert(1 <= *m && *m <= 12);

  /* Now only day can be out of bounds (year may also be out of bounds
   * for a datetime object, but we don't care about that here).
   * If day is out of bounds, what to do is arguable, but at least the
   * method here is principled and explainable.
   */
  dim = days_in_month(*y, *m);
  if (*d < 1 || *d > dim) {
    /* Move day-1 days from the first of the month.  First try to
     * get off cheap if we're only one day out of range
     * (adjustments for timezone alone can't be worse than that).
     */
    if (*d == 0) {
      --*m;
      if (*m > 0) {
        *d = days_in_month(*y, *m);
      } else {
        --*y;
        *m = 12;
        *d = 31;
      }
    } else if (*d == dim + 1) {
      /* move forward a day */
      ++*m;
      *d = 1;
      if (*m > 12) {
        *m = 1;
        ++*y;
      }
    } else {
      int ordinal = ymd_to_ord(*y, *m, 1) + *d - 1;
      if (ordinal < 1 || ordinal > MAXORDINAL) {
        goto error;
      } else {
        ord_to_ymd(ordinal, y, m, d);
        return 0;
      }
    }
  }
  assert(*m > 0);
  assert(*d > 0);
  if (MINYEAR <= *y && *y <= MAXYEAR) {
    return 0;
  }
error:
  return -1;
}

/* Fiddle out-of-bounds months and days so that the result makes some kind
 * of sense.  The parameters are both inputs and outputs.  Returns < 0 on
 * failure, where failure means the adjusted year is out of bounds.
 */
static int normalize_date(int *year, int *month, int *day) {
  return normalize_y_m_d(year, month, day);
}

/* Force all the datetime fields into range.  The parameters are both
 * inputs and outputs.  Returns < 0 on error.
 */
static int normalize_datetime(int *year, int *month, int *day, int *hour,
                              int *minute, int *second, int *microsecond) {
  normalize_pair(second, microsecond, 1000000);
  normalize_pair(minute, second, 60);
  normalize_pair(hour, minute, 60);
  normalize_pair(day, hour, 24);
  return normalize_date(year, month, day);
}

/* ---------------------------------------------------------------------------
 * String parsing utilities and helper functions
 */

static const char *parse_digits(const char *ptr, int *var,
                                std::size_t num_digits) {
  for (std::size_t i = 0; i < num_digits; ++i) {
    unsigned int tmp = (unsigned int)(*(ptr++) - '0');
    if (tmp > 9) {
      return nullptr;
    }
    *var *= 10;
    *var += (signed int)tmp;
  }

  return ptr;
}

[[gnu::unused]] static int parse_isoformat_date(const char *dtstr, int *year,
                                                int *month, int *day) {
  /* Parse the date components of the result of date.isoformat()
   *
   *  Return codes:
   *       0:  Success
   *      -1:  Failed to parse date component
   *      -2:  Failed to parse dateseparator
   */
  const char *p = dtstr;
  p = parse_digits(p, year, 4);
  if (nullptr == p) {
    return -1;
  }

  if (*(p++) != '-') {
    return -2;
  }

  p = parse_digits(p, month, 2);
  if (nullptr == p) {
    return -1;
  }

  if (*(p++) != '-') {
    return -2;
  }

  p = parse_digits(p, day, 2);
  if (p == nullptr) {
    return -1;
  }

  return 0;
}

static int parse_hh_mm_ss_ff(const char *tstr, const char *tstr_end, int *hour,
                             int *minute, int *second, int *microsecond) {
  const char *p = tstr;
  const char *p_end = tstr_end;
  int *vals[3] = {hour, minute, second};

  // Parse [HH[:MM[:SS]]]
  for (std::size_t i = 0; i < 3; ++i) {
    p = parse_digits(p, vals[i], 2);
    if (nullptr == p) {
      return -3;
    }

    char c = *(p++);
    if (p >= p_end) {
      return c != '\0';
    } else if (c == ':') {
      continue;
    } else if (c == '.') {
      break;
    } else {
      return -4;  // Malformed time separator
    }
  }

  // Parse .fff[fff]
  std::size_t len_remains = p_end - p;
  if (!(len_remains == 6 || len_remains == 3)) {
    return -3;
  }

  p = parse_digits(p, microsecond, len_remains);
  if (nullptr == p) {
    return -3;
  }

  if (len_remains == 3) {
    *microsecond *= 1000;
  }

  // Return 1 if it's not the end of the string
  return *p != '\0';
}

[[gnu::unused]] static int parse_isoformat_time(const char *dtstr,
                                                std::size_t dtlen, int *hour,
                                                int *minute, int *second,
                                                int *microsecond, int *tzoffset,
                                                int *tzmicrosecond) {
  // Parse the time portion of a datetime.isoformat() string
  //
  // Return codes:
  //      0:  Success (no tzoffset)
  //      1:  Success (with tzoffset)
  //     -3:  Failed to parse time component
  //     -4:  Failed to parse time separator
  //     -5:  Malformed timezone string

  const char *p = dtstr;
  const char *p_end = dtstr + dtlen;

  const char *tzinfo_pos = p;
  do {
    if (*tzinfo_pos == '+' || *tzinfo_pos == '-') {
      break;
    }
  } while (++tzinfo_pos < p_end);

  int rv =
      parse_hh_mm_ss_ff(dtstr, tzinfo_pos, hour, minute, second, microsecond);

  if (rv < 0) {
    return rv;
  } else if (tzinfo_pos == p_end) {
    // We know that there's no time zone, so if there's stuff at the
    // end of the string it's an error.
    if (rv == 1) {
      return -5;
    } else {
      return 0;
    }
  }

  // Parse time zone component
  // Valid formats are:
  //    - +HH:MM           (len  6)
  //    - +HH:MM:SS        (len  9)
  //    - +HH:MM:SS.ffffff (len 16)
  std::size_t tzlen = p_end - tzinfo_pos;
  if (!(tzlen == 6 || tzlen == 9 || tzlen == 16)) {
    return -5;
  }

  int tzsign = (*tzinfo_pos == '-') ? -1 : 1;
  tzinfo_pos++;
  int tzhour = 0, tzminute = 0, tzsecond = 0;
  rv = parse_hh_mm_ss_ff(tzinfo_pos, p_end, &tzhour, &tzminute, &tzsecond,
                         tzmicrosecond);

  *tzoffset = tzsign * ((tzhour * 3600) + (tzminute * 60) + tzsecond);
  *tzmicrosecond *= tzsign;

  return rv ? -5 : 1;
}

std::string format_ctime(int year, int month, int day, int hour, int minute,
                         int second) {
  static const char *DayNames[] = {"Mon", "Tue", "Wed", "Thu",
                                   "Fri", "Sat", "Sun"};
  static const char *MonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  int wday = idea::datetime::weekday(year, month, day);

  return fmt::format("{} {} {:2d} {:02d}:{:02d}:{02d} {:04d}", DayNames[wday],
                     MonthNames[month - 1], day, hour, minute, second, year);
}

timedelta::timedelta(int days, int seconds, int microseconds, bool normalize) {
  if (normalize) {
    normalize_d_s_us(&days, &seconds, &microseconds);
  }

  if (check_delta_day_range(days) < 0) {
    throw 1;
  }

  set_days(days);
  set_seconds(seconds);
  set_microseconds(microseconds);
}

static inline long accum(long sofar, long num, long factor) {
  return num * factor + sofar;
}

timedelta::timedelta(const timedelta &other)
    : days_(other.days_),
      seconds_(other.seconds_),
      microseconds_(other.microseconds_) {}

timedelta::timedelta(int days, int seconds, int microseconds)
    : timedelta(days, seconds, microseconds, true) {}

timedelta::timedelta(int days, int seconds, int microseconds, int milliseconds,
                     int minutes, int hours, int weeks) {
  long x = 0;

  if (microseconds > 0) {
    x = accum(x, microseconds, us_per_us);
  }
  if (milliseconds > 0) {
    x = accum(x, milliseconds, us_per_ms);
  }
  if (seconds > 0) {
    x = accum(x, seconds, us_per_second);
  }
  if (minutes > 0) {
    x = accum(x, minutes, us_per_minute);
  }
  if (hours > 0) {
    x = accum(x, hours, us_per_hour);
  }
  if (days > 0) {
    x = accum(x, days, us_per_day);
  }
  if (weeks > 0) {
    x = accum(x, weeks, us_per_week);
  }

  *this = microseconds_to_delta(x);
}

long timedelta::delta_to_microseconds() const {
  return (seconds_per_day * days() + seconds()) * us_per_second +
         microseconds();
}

timedelta timedelta::microseconds_to_delta(long us) {
  long s;
  long d = 0;

  s = divmod(us, us_per_second, &us);
  if (s != 0) {
    d = divmod(s, seconds_per_day, &s);
  }
  return timedelta(static_cast<int>(d), static_cast<int>(s),
                   static_cast<int>(us), false);
}

timedelta timedelta::operator+(const timedelta &rhs) const {
  int _days = days() + rhs.days();
  int _seconds = seconds() + rhs.seconds();
  int _microseconds = microseconds() + rhs.microseconds();
  return timedelta(_days, _seconds, _microseconds, true);
}

timedelta &timedelta::operator+=(const timedelta &rhs) {
  *this = *this + rhs;
  return *this;
}

timedelta timedelta::operator-(const timedelta &rhs) const {
  int _days = days() - rhs.days();
  int _seconds = seconds() - rhs.seconds();
  int _microseconds = microseconds() - rhs.microseconds();
  return timedelta(_days, _seconds, _microseconds, true);
}

timedelta &timedelta::operator-=(const timedelta &rhs) {
  *this = *this - rhs;
  return *this;
}

timedelta timedelta::operator/(const timedelta &rhs) const {
  auto rhs_us = rhs.delta_to_microseconds();
  if (rhs_us == 0) {
    throw 1;
  }
  auto lhs_us = delta_to_microseconds();
  return microseconds_to_delta(div(lhs_us, rhs_us));
}

timedelta &timedelta::operator/=(const timedelta &rhs) {
  *this = *this / rhs;
  return *this;
}

int timedelta::cmp(const timedelta &rhs) const {
  int diff = days() - rhs.days();
  if (diff == 0) {
    diff = seconds() - rhs.seconds();
    if (diff == 0) {
      diff = microseconds() - rhs.microseconds();
    }
  }
  return diff;
}

timedelta timedelta::operator+() const { return *this; }

timedelta timedelta::operator-() const {
  return timedelta(-days(), -seconds(), -microseconds(), true);
}

timedelta timedelta::operator*(int n) const {
  long us = delta_to_microseconds();
  us *= n;
  return microseconds_to_delta(us);
}

timedelta &timedelta::operator*=(int n) {
  *this = *this * n;
  return *this;
}

timedelta timedelta::operator/(int n) const {
  if (n == 0) {
    throw 1;
  }
  long us = delta_to_microseconds();
  us = div(us, us_per_second);
  return microseconds_to_delta(us);
}

timedelta &timedelta::operator/=(int n) {
  *this = *this / n;
  return *this;
}

timedelta timedelta::operator%(const timedelta &rhs) const {
  auto lhs_us = delta_to_microseconds();
  auto rhs_us = rhs.delta_to_microseconds();
  long us = mod(lhs_us, rhs_us);
  return microseconds_to_delta(us);
}

timedelta &timedelta::operator%=(const timedelta &rhs) {
  *this = *this % rhs;
  return *this;
}

long timedelta::total_seconds() const {
  auto total_us = delta_to_microseconds();
  return div(total_us, us_per_second);
}

timedelta timedelta::abs() const {
  if (days() < 0) {
    return -(*this);
  } else {
    return *this;
  }
}

std::string timedelta::str() const {
  int us = microseconds();
  int s = seconds();
  int m = divmod(s, 60, &s);
  int h = divmod(m, 60, &m);
  int d = days();
  if (d != 0) {
    if (us != 0) {
      return fmt::format("{} day{}, {}:{:02d}:{:02d}.{:06d}", d,
                         (d == 1 || d == -1) ? "" : "s", h, m, s, us);
    } else {
      return fmt::format("{} day{}, {}:{:02d}:{:02d}", d,
                         (d == 1 || d == -1) ? "" : "s", h, m, s);
    }
  } else {
    if (us != 0) {
      return fmt::format("{}:{:02d}:{:02d}.{:06d}", h, m, s, us);
    } else {
      return fmt::format("{}:{:02d}:{:02d}", h, m, s);
    }
  }
}

std::string timedelta::repr() const {
  if (microseconds() != 0) {
    return fmt::format("timedelta({}, {}, {})", days(), seconds(),
                       microseconds());
  }
  if (seconds() != 0) {
    return fmt::format("timedelta({}, {})", seconds(), microseconds());
  }
  return fmt::format("timedelta({})", days());
}

date::date(int year, int month, int day) : date(year, month, day, true) {}

date::date(int year, int month, int day, bool check) {
  if (check) {
    if (check_date_args(year, month, day) < 0) {
      throw 1;
    }
  }
  set_fileds(year, month, day);
}

date date::today() { return fromtimestamp(::time(nullptr)); }

date date::fromtimestamp(time_t timestamp) {
  struct tm tm;
  localtime_r(&timestamp, &tm);
  return date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

date date::fromordinal(int ordinal) {
  if (ordinal < 1) {
    throw 1;
  }

  int y;
  int m;
  int d;
  ord_to_ymd(ordinal, &y, &m, &d);
  return date(y, m, d);
}

int date::weekday() const {
  return ::idea::datetime::weekday(year(), month(), day());
}

date date::operator+(const timedelta &delta) const {
  int y = year();
  int m = month();
  int d = day() + delta.days();
  if (normalize_date(&y, &m, &d) >= 0) {
    return date(y, m, d, false);
  }
  throw 1;
}

date &date::operator+=(const timedelta &delta) {
  *this = *this + delta;
  return *this;
}

date date::operator-(const timedelta &delta) const {
  int y = year();
  int m = month();
  int d = day() - delta.days();
  if (normalize_date(&y, &m, &d) >= 0) {
    return date(y, m, d, false);
  }
  throw 1;
}

date &date::operator-=(const timedelta &delta) {
  *this = *this - delta;
  return *this;
}

timedelta date::operator-(const date &rhs) const {
  int lhs_ord = ymd_to_ord(year(), month(), day());
  int rhs_old = ymd_to_ord(rhs.year(), rhs.month(), rhs.day());
  return timedelta(lhs_ord - rhs_old, 0, 0, false);
}

int date::cmp(const date &rhs) const {
  return memcmp(data_, rhs.data_, sizeof(data_));
}

std::string date::ctime() const {
  return format_ctime(year(), month(), day(), 0, 0, 0);
}

std::string date::str() const {
  return fmt::format("{:04d}-{:02d}-{:02d}", year(), month(), day());
}

std::string date::repr() const {
  return fmt::format("date({}, {}, {})", year(), month(), day());
}

time::time(int hour, int minute, int second, int usecond) {
  if (check_time_args(hour, minute, second, usecond) < 0) {
    throw 1;
  }

  set_hour(hour);
  set_minute(minute);
  set_second(second);
  set_microsecond(usecond);
}

std::string time::isoformat() const {
  int us = microsecond();
  if (us != 0) {
    return fmt::format("{:02d}:{:02d}:{:02d}.{:06d}", hour(), minute(),
                       second(), us);
  } else {
    return fmt::format("{:02d}:{:02d}:{:02d}", hour(), minute(), second());
  }
}

int time::cmp(const time &rhs) const {
  return memcmp(data_, rhs.data_, sizeof(data_));
}

std::string time::str() const { return isoformat(); }

std::string time::repr() const {
  int h = hour();
  int m = minute();
  int s = second();
  int us = microsecond();

  if (us != 0) {
    return fmt::format("time({}, {}, {}, {})", h, m, s, us);
  } else if (s != 0) {
    return fmt::format("time({}, {}, {})", h, m, s);
  } else {
    return fmt::format("time({}, {})", h, m);
  }
}

datetime::datetime(int year, int month, int day, int hour, int minute,
                   int second, int usecond) {
  if (check_date_args(year, month, day) < 0) {
    throw 1;
  }
  if (check_time_args(hour, minute, second, usecond) < 0) {
    throw 1;
  }
  set_year(year);
  set_month(month);
  set_day(day);
  set_hour(hour);
  set_minute(minute);
  set_second(second);
  set_microsecond(usecond);
}

datetime::datetime(const datetime &other) {
  memcpy(data_, other.data_, sizeof(data_));
}

datetime datetime::now() {
  return fromtimestamp(std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());
}

datetime datetime::fromtimestamp(time_t timestamp_us) {
  time_t us = timestamp_us % 1000000;
  time_t timestamp = timestamp_us / 1000000;
  struct tm tm;

  localtime_r(&timestamp, &tm);
  if (tm.tm_sec > 59) {
    tm.tm_sec = 59;
  }

  return datetime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec, us);
}

datetime datetime::operator+(const timedelta &delta) const {
  int y = year();
  int m = month();
  int d = day() + delta.days();
  int h = hour();
  int min = minute();
  int s = second() + delta.seconds();
  int us = microsecond() + delta.microseconds();

  if (normalize_datetime(&y, &m, &d, &h, &min, &s, &us) < 0) {
    throw 1;
  }
  return datetime(y, m, d, h, min, s, us);
}

datetime &datetime::operator+=(const timedelta &delta) {
  *this = *this + delta;
  return *this;
}

datetime datetime::operator-(const timedelta &delta) const {
  int y = year();
  int m = month();
  int d = day() - delta.days();
  int h = hour();
  int min = minute();
  int s = second() - delta.seconds();
  int us = microsecond() - delta.microseconds();

  if (normalize_datetime(&y, &m, &d, &h, &min, &s, &us) < 0) {
    throw 1;
  }
  return datetime(y, m, d, h, min, s, us);
}

datetime &datetime::operator-=(const timedelta &delta) {
  *this = *this - delta;
  return *this;
}

timedelta datetime::operator-(const datetime &rhs) const {
  auto delta_d = ymd_to_ord(year(), month(), day()) -
                 ymd_to_ord(rhs.year(), rhs.month(), rhs.day());
  auto delta_s = (hour() - rhs.hour()) * 3600 + (minute() - rhs.minute()) * 60 +
                 (second() - rhs.second());
  auto delta_us = microsecond() - rhs.microsecond();
  return timedelta(delta_d, delta_s, delta_us);
}

std::string datetime::ctime() const {
  return format_ctime(year(), month(), day(), hour(), minute(), second());
}

int datetime::cmp(const datetime &rhs) const {
  return memcmp(data_, rhs.data_, sizeof(data_));
}

std::string datetime::str() const {
  char sep = 'T';
  int us = microsecond();
  if (us != 0) {
    return fmt::format("{:04d}-{:02d}-{:02d}{}{:02d}:{:02d}:{:02d}.{:06d}",
                       year(), month(), day(), sep, hour(), minute(), second(),
                       us);
  } else {
    return fmt::format("{:04d}-{:02d}-{:02d}{}{:02d}:{:02d}:{:02d}", year(),
                       month(), day(), sep, hour(), minute(), second());
  }
}

std::string datetime::repr() const {
  if (microsecond() != 0) {
    return fmt::format("datetime({}, {}, {}, {}, {}, {}, {})", year(), month(),
                       day(), hour(), minute(), second(), microsecond());
  } else if (second() != 0) {
    return fmt::format("datetime({}, {}, {}, {}, {}, {})", year(), month(),
                       day(), hour(), minute(), second());
  } else {
    return fmt::format("datetime({}, {}, {}, {}, {})", year(), month(), day(),
                       hour(), minute());
  }
}

}  // namespace datetime
