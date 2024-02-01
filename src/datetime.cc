#include "xyu/datetime.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "fmt/format.h"

namespace datetime {

/* k = i+j overflows iff k differs in sign from both inputs,
 * iff k^i has sign bit set and k^j has sign bit set,
 * iff (k^i)&(k^j) has sign bit set.
 */
#define SIGNED_ADD_OVERFLOWED(RESULT, I, J) ((((RESULT) ^ (I)) & ((RESULT) ^ (J))) < 0)

/* date(1970,1,1).toordinal() == 719163 */
#define EPOCH_SECONDS (719163LL * 24 * 60 * 60)

static constexpr long kUsPerUs = 1L;
static constexpr long kUsPerMs = 1000L;
static constexpr long kUsPerSecond = kUsPerMs * 1000L;
static constexpr long kUsPerMinute = kUsPerSecond * 60L;
static constexpr long kUsPerHour = kUsPerMinute * 60L;
static constexpr long kUsPerDay = kUsPerHour * 24L;
static constexpr long kUsPerWeek = kUsPerDay * 7L;
static constexpr long kSecondsPerDay = 3600L * 24L;

/* As of version 2015f max fold in IANA database is
 * 23 hours at 1969-09-30 13:00:00 in Kwajalein. */
static constexpr long long kMaxFoldSeconds = 24 * 3600;
/* NB: date(1970,1,1).toordinal() == 719163 */
static constexpr long long kEpoch = 719163LL * 24 * 60 * 60;

static const char* kDayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

static const char* kDayFullNames[] = {"Monday", "Tuesday", "Wensday", "Thurday",
                                      "Friday", "Satday",  "Sunday"};

static const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static const char* kMonthFullNames[] = {"January",   "February", "March",    "April",
                                        "May",       "June",     "July",     "August",
                                        "September", "October",  "November", "December"};

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
static long divmod(IntType x, IntType y, IntType* r) {
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
static const int kDaysInMonth[] = {0, /* unused; this vector uses 1-based indexing */
                                   31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static const int kDaysBeforeMonth[] = {0, /* unused; this vector uses 1-based indexing */
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
    return kDaysInMonth[month];
  }
}

/* year, month -> number of days in year preceding first day of month */
static inline int days_before_month(int year, int month) {
  int days;

  assert(month >= 1);
  assert(month <= 12);
  days = kDaysBeforeMonth[month];
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

/* ordinal -> year, month, day, considering 01-Jan-0001 as day 1. */
static void ord_to_ymd(int ordinal, int* year, int* month, int* day) {
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
  preceding = (kDaysBeforeMonth[*month] + (*month > 2 && leapyear));
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
static inline int weekday(int year, int month, int day) {
  return (ymd_to_ord(year, month, day) + 6) % 7;
}

/* Ordinal of the Monday starting week 1 of the ISO year.  Week 1 is the
 * first calendar week containing a Thursday.
 */
static int iso_week1_monday(int year) {
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

static void check_delta_day_range(int days) {
  if (-kMaxDeltaDays <= days && days <= kMaxDeltaDays) {
    return;
  }
  throw std::out_of_range(fmt::format("Out of range: timedelta.days:{} valid_range:[{},{}]", days,
                                      -kMaxDeltaDays, kMaxDeltaDays));
}

void check_date_args(int year, int month, int day) {
  if (year < kMinYear || year > kMaxYear) {
    throw std::out_of_range(fmt::format("check_date_args: year out of range: year={}", year));
  }
  if (month < 1 || month > 12) {
    throw std::out_of_range(fmt::format("check_date_args: month out of range: month={}", month));
  }
  if (day < 1 || day > days_in_month(year, month)) {
    throw std::out_of_range(fmt::format("check_date_args: day out of range: day={}", day));
  }
}

static void check_time_args(int h, int m, int s, int us) {
  if (h < 0 || h > 23) {
    throw std::out_of_range(fmt::format("check_time_args: hour out of range: hour={}", h));
  }
  if (m < 0 || m > 59) {
    throw std::out_of_range(fmt::format("check_time_args: minute out of range: minute={}", m));
  }
  if (s < 0 || s > 59) {
    throw std::out_of_range(fmt::format("check_time_args: second out of range: second={}", s));
  }
  if (us < 0 || us > 999999) {
    throw std::out_of_range(
        fmt::format("check_time_args: microsecond out of range: microsecond={}", us));
  }
}

/* ---------------------------------------------------------------------------
 * Normalization utilities.
 */

/* One step of a mixed-radix conversion.  A "hi" unit is equivalent to
 * factor "lo" unxyts.  factor must be > 0.  If *lo is less than 0, or
 * at least factor, enough of *lo is converted into "hi" units so that
 * 0 <= *lo < factor.  The input values must be such that int overflow
 * is impossible.
 */
static void normalize_pair(int* hi, int* lo, int factor) {
  assert(factor > 0);
  assert(lo != hi);
  if (*lo < 0 || *lo >= factor) {
    const int num_hi = divmod(*lo, factor, lo);
    const int new_hi = *hi + num_hi;
    assert(!SIGNED_ADD_OVERFLOWED(new_hi, *hi, num_hi));
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
static void normalize_d_s_us(int* d, int* s, int* us) {
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
static int normalize_y_m_d(int* y, int* m, int* d) {
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
      if (ordinal < 1 || ordinal > kMaxOrdinal) {
        goto error;
      } else {
        ord_to_ymd(ordinal, y, m, d);
        return 0;
      }
    }
  }
  assert(*m > 0);
  assert(*d > 0);
  if (kMinYear <= *y && *y <= kMaxYear) {
    return 0;
  }
error:
  return -1;
}

/* Fiddle out-of-bounds months and days so that the result makes some kind
 * of sense.  The parameters are both inputs and outputs.  Returns < 0 on
 * failure, where failure means the adjusted year is out of bounds.
 */
static int normalize_date(int* year, int* month, int* day) {
  return normalize_y_m_d(year, month, day);
}

/* Force all the datetime fields into range.  The parameters are both
 * inputs and outputs.  Returns < 0 on error.
 */
static int normalize_datetime(int* year, int* month, int* day, int* hour, int* minute, int* second,
                              int* microsecond) {
  normalize_pair(second, microsecond, 1000000);
  normalize_pair(minute, second, 60);
  normalize_pair(hour, minute, 60);
  normalize_pair(day, hour, 24);
  return normalize_date(year, month, day);
}

/* ---------------------------------------------------------------------------
 * String parsing utilities and helper functions
 */

static const char* parse_digits(const char* ptr, int* var, std::size_t num_digits) {
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

[[gnu::unused]] static int parse_isoformat_date(const char* dtstr, int* year, int* month,
                                                int* day) {
  /* Parse the date components of the result of date.isoformat()
   *
   *  Return codes:
   *       0:  Success
   *      -1:  Failed to parse date component
   *      -2:  Failed to parse dateseparator
   */
  const char* p = dtstr;
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

static int parse_hh_mm_ss_ff(const char* tstr, const char* tstr_end, int* hour, int* minute,
                             int* second, int* microsecond) {
  const char* p = tstr;
  const char* p_end = tstr_end;
  int* vals[3] = {hour, minute, second};

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

[[gnu::unused]] static int parse_isoformat_time(const char* dtstr, std::size_t dtlen, int* hour,
                                                int* minute, int* second, int* microsecond,
                                                int* tzoffset, int* tzmicrosecond) {
  // Parse the time portion of a datetime.isoformat() string
  //
  // Return codes:
  //      0:  Success (no tzoffset)
  //      1:  Success (with tzoffset)
  //     -3:  Failed to parse time component
  //     -4:  Failed to parse time separator
  //     -5:  Malformed timezone string

  const char* p = dtstr;
  const char* p_end = dtstr + dtlen;

  const char* tzinfo_pos = p;
  do {
    if (*tzinfo_pos == '+' || *tzinfo_pos == '-') {
      break;
    }
  } while (++tzinfo_pos < p_end);

  int rv = parse_hh_mm_ss_ff(dtstr, tzinfo_pos, hour, minute, second, microsecond);

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
  rv = parse_hh_mm_ss_ff(tzinfo_pos, p_end, &tzhour, &tzminute, &tzsecond, tzmicrosecond);

  *tzoffset = tzsign * ((tzhour * 3600) + (tzminute * 60) + tzsecond);
  *tzmicrosecond *= tzsign;

  return rv ? -5 : 1;
}

std::string format_ctime(int year, int month, int day, int hour, int minute, int second) {
  int wday = datetime::weekday(year, month, day);

  return fmt::format("{} {} {:2d} {:02d}:{:02d}:{:02d} {:04d}", kDayNames[wday],
                     kMonthNames[month - 1], day, hour, minute, second, year);
}

static long long utc_to_seconds(int year, int month, int day, int hour, int minute, int second) {
  long long ordinal;

  if (year < kMinYear || year > kMaxYear) {
    throw std::out_of_range(fmt::format("Year {} is out of range", year));
  }

  ordinal = ymd_to_ord(year, month, day);
  return ((ordinal * 24 + hour) * 60 + minute) * 60 + second;
}

static long long local(long long u) {
  struct tm local_time;
  time_t t;
  u -= kEpoch;
  t = u;
  if (t != u) {
    throw std::overflow_error("Timestamp out of range for platform time_t");
  }
  localtime_r(&t, &local_time);
  return utc_to_seconds(local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                        local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
}

static long long local_to_seconds(int year, int month, int day, int hour, int minute, int second,
                                  int fold) {
  long long t, a, b, u1, u2, t1, t2, lt;
  t = utc_to_seconds(year, month, day, hour, minute, second);
  /* Our goal is to solve t = local(u) for u. */
  lt = local(t);
  if (lt == -1) return -1;
  a = lt - t;
  u1 = t - a;
  t1 = local(u1);
  if (t1 == -1) return -1;
  if (t1 == t) {
    /* We found one solution, but it may not be the one we need.
     * Look for an earlier solution (if `fold` is 0), or a
     * later one (if `fold` is 1). */
    if (fold)
      u2 = u1 + kMaxFoldSeconds;
    else
      u2 = u1 - kMaxFoldSeconds;
    lt = local(u2);
    if (lt == -1) return -1;
    b = lt - u2;
    if (a == b) return u1;
  } else {
    b = t1 - u1;
    assert(a != b);
  }
  u2 = t - b;
  t2 = local(u2);
  if (t2 == -1) return -1;
  if (t2 == t) return u2;
  if (t1 == t) return u1;
  /* We have found both offsets a and b, but neither t - a nor t - b is
   * a solution.  This means t is in the gap. */
  return fold ? std::min(u1, u2) : std::max(u1, u2);
}

timedelta::timedelta(int days, int seconds, int microseconds, detail::NonNormTag)
    : days_(days), seconds_(seconds), microseconds_(microseconds) {
  check_delta_day_range(days);
}

timedelta::timedelta(int days, int seconds, int microseconds, detail::NonNormNonCheckTag)
    : days_(days), seconds_(seconds), microseconds_(microseconds) {}

timedelta::timedelta(const timedelta& other)
    : days_(other.days_), seconds_(other.seconds_), microseconds_(other.microseconds_) {}

timedelta::timedelta(int days, int seconds, int microseconds) {
  normalize_d_s_us(&days, &seconds, &microseconds);

  check_delta_day_range(days);

  days_ = days;
  seconds_ = seconds;
  microseconds_ = microseconds;
}

timedelta::timedelta(int days, int seconds, int microseconds, int milliseconds, int minutes,
                     int hours, int weeks) {
  // TODO: Check overflow
  namespace chrono = std::chrono;
  chrono::microseconds us = chrono::microseconds{microseconds} +
                            chrono::milliseconds{milliseconds} + chrono::seconds{seconds} +
                            chrono::minutes{minutes} + chrono::hours{hours} + chrono::days{days} +
                            chrono::weeks{weeks};
  frommicroseconds(us.count());
}

// TODO: Check overflow
timedelta::timedelta(std::chrono::weeks weeks)
    : timedelta(std::chrono::duration_cast<std::chrono::microseconds>(weeks)) {}

// TODO: Check overflow
timedelta::timedelta(std::chrono::days days)
    : timedelta(std::chrono::duration_cast<std::chrono::microseconds>(days)) {}

// TODO: Check overflow
timedelta::timedelta(std::chrono::hours hours)
    : timedelta(std::chrono::duration_cast<std::chrono::microseconds>(hours)) {}

// TODO: Check overflow
timedelta::timedelta(std::chrono::minutes minutes)
    : timedelta(std::chrono::duration_cast<std::chrono::microseconds>(minutes)) {}

// TODO: Check overflow
timedelta::timedelta(std::chrono::seconds seconds)
    : timedelta(std::chrono::duration_cast<std::chrono::microseconds>(seconds)) {
}  // TODO: Check overflow

// TODO: Check overflow
timedelta::timedelta(std::chrono::milliseconds milliseconds)
    : timedelta(std::chrono::duration_cast<std::chrono::microseconds>(milliseconds)) {}

timedelta::timedelta(std::chrono::microseconds microseconds) {
  frommicroseconds(microseconds.count());
}

long timedelta::delta_to_microseconds() const {
  return (kSecondsPerDay * days() + seconds()) * kUsPerSecond + microseconds();
}

timedelta timedelta::microseconds_to_delta(long us) {
  timedelta delta;
  delta.frommicroseconds(us);
  return delta;
}

void timedelta::frommicroseconds(long us) {
  long s;
  long d = 0;

  s = divmod(us, kUsPerSecond, &us);
  if (s != 0) {
    d = divmod(s, kSecondsPerDay, &s);
    check_delta_day_range(d);
  }
  days_ = static_cast<int>(d);
  seconds_ = static_cast<int>(s);
  microseconds_ = static_cast<int>(us);
}

timedelta& timedelta::operator=(const timedelta& rhs) {
  days_ = rhs.days();
  seconds_ = rhs.seconds();
  microseconds_ = rhs.microseconds();
  return *this;
}

timedelta timedelta::operator+(const timedelta& rhs) const {
  int _days = days() + rhs.days();
  int _seconds = seconds() + rhs.seconds();
  int _microseconds = microseconds() + rhs.microseconds();
  return timedelta(_days, _seconds, _microseconds);
}

timedelta& timedelta::operator+=(const timedelta& rhs) {
  *this = *this + rhs;
  return *this;
}

timedelta timedelta::operator-(const timedelta& rhs) const {
  int _days = days() - rhs.days();
  int _seconds = seconds() - rhs.seconds();
  int _microseconds = microseconds() - rhs.microseconds();
  return timedelta(_days, _seconds, _microseconds);
}

timedelta& timedelta::operator-=(const timedelta& rhs) {
  *this = *this - rhs;
  return *this;
}

timedelta timedelta::operator/(const timedelta& rhs) const {
  auto rhs_us = rhs.delta_to_microseconds();
  if (rhs_us == 0) {
    throw std::runtime_error("Divide zero timedelta");
  }
  auto lhs_us = delta_to_microseconds();
  return microseconds_to_delta(div(lhs_us, rhs_us));
}

timedelta& timedelta::operator/=(const timedelta& rhs) {
  *this = *this / rhs;
  return *this;
}

timedelta timedelta::operator+() const { return *this; }

timedelta timedelta::operator-() const { return timedelta(-days(), -seconds(), -microseconds()); }

timedelta timedelta::operator*(int n) const {
  long us = delta_to_microseconds();
  us *= n;
  return microseconds_to_delta(us);
}

timedelta& timedelta::operator*=(int n) {
  *this = *this * n;
  return *this;
}

timedelta timedelta::operator/(int n) const {
  if (n == 0) {
    throw std::runtime_error("Divide zero timedelta");
  }
  long us = delta_to_microseconds();
  us = div(us, kUsPerSecond);
  return microseconds_to_delta(us);
}

timedelta& timedelta::operator/=(int n) {
  *this = *this / n;
  return *this;
}

timedelta timedelta::operator%(const timedelta& rhs) const {
  auto lhs_us = delta_to_microseconds();
  auto rhs_us = rhs.delta_to_microseconds();
  long us = mod(lhs_us, rhs_us);
  return microseconds_to_delta(us);
}

timedelta& timedelta::operator%=(const timedelta& rhs) {
  *this = *this % rhs;
  return *this;
}

long timedelta::total_seconds() const {
  auto total_us = delta_to_microseconds();
  return div(total_us, kUsPerSecond);
}

long timedelta::total_milliseconds() const {
  auto total_us = delta_to_microseconds();
  return div(total_us, kUsPerMs);
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
      return fmt::format("{} day{}, {}:{:02d}:{:02d}.{:06d}", d, (d == 1 || d == -1) ? "" : "s", h,
                         m, s, us);
    } else {
      return fmt::format("{} day{}, {}:{:02d}:{:02d}", d, (d == 1 || d == -1) ? "" : "s", h, m, s);
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
    return fmt::format("timedelta({}, {}, {})", days(), seconds(), microseconds());
  }
  if (seconds() != 0) {
    return fmt::format("timedelta({}, {})", seconds(), microseconds());
  }
  return fmt::format("timedelta({})", days());
}

date::date(int year, int month, int day) {
  check_date_args(year, month, day);
  set_fileds(year, month, day);
}

date::date(int year, int month, int day, detail::NonCheckTag) { set_fileds(year, month, day); }

date date::today() {
  return fromtimestamp(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()));
}

date date::fromisoformat(const std::string& date_string) {
  int year = 0, month = 0, day = 0;

  int rv;
  if (date_string.size() == 10) {
    rv = parse_isoformat_date(date_string.c_str(), &year, &month, &day);
  } else {
    rv = -1;
  }

  if (rv < 0) {
    throw std::invalid_argument(
        fmt::format("date::fromisoformat: Invalid isoformat string: {}", date_string));
  }
  return date(year, month, day);
}

date date::fromtimestamp(std::chrono::microseconds timestamp) {
  return fromtimestamp(std::chrono::duration_cast<std::chrono::seconds>(timestamp));
}

date date::fromtimestamp(std::chrono::seconds timestamp) {
  struct tm tm;
  auto t = timestamp.count();
  localtime_r(&t, &tm);
  return date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

date date::fromordinal(int ordinal) {
  if (ordinal < 1) {
    throw std::invalid_argument(fmt::format("date::fromordinal: Invalid ordinal: {}", ordinal));
  }

  int y;
  int m;
  int d;
  ord_to_ymd(ordinal, &y, &m, &d);
  return date(y, m, d);
}

date date::fromisocalendar(const IsoCalendarDate& iso_calendar) {
  int y = iso_calendar.year;
  if (y < kMinYear || y > kMaxYear) {
    throw std::out_of_range(fmt::format("date::fromisocalendar: Year out of range: {}", y));
  }

  int week = iso_calendar.week;
  if (week <= 0 || week >= 53) {
    int out_of_range = 1;
    if (week == 53) {
      // ISO years have 53 weeks in it on years starting with a Thursday
      // and on leap years starting on Wednesday
      int first_weekday = ::datetime::weekday(y, 1, 1);
      if (first_weekday == 3 || (first_weekday == 2 && is_leap(y))) {
        out_of_range = 0;
      }
    }

    if (out_of_range) {
      throw std::out_of_range(fmt::format("date::fromisocalendar: Week out of range: {}", week));
    }
  }

  int d = iso_calendar.weekday;
  if (d <= 0 || d >= 8) {
    throw std::out_of_range(
        fmt::format("date::fromisocalendar: day out of range: {} (valid: [1, 7])", d));
  }

  // Convert (Y, W, D) to (Y, M, D) in-place
  int day_1 = iso_week1_monday(y);

  int mon = week;
  int day_offset = (mon - 1) * 7 + d - 1;

  ord_to_ymd(day_1 + day_offset, &y, &mon, &d);

  return date(y, mon, d);
}

int date::weekday() const { return ::datetime::weekday(year(), month(), day()); }

int date::toordinal() const { return ymd_to_ord(year(), month(), day()); }

IsoCalendarDate date::isocalendar() const {
  int y = year();
  int week1_monday = iso_week1_monday(y);
  int today = ymd_to_ord(y, month(), day());
  int week;
  int d;

  week = divmod(today - week1_monday, 7, &d);
  if (week < 0) {
    --y;
    week1_monday = iso_week1_monday(y);
    week = divmod(today - week1_monday, 7, &d);
  } else if (week >= 52 && today >= iso_week1_monday(y + 1)) {
    ++y;
    week = 0;
  }

  return IsoCalendarDate{y, week + 1, d + 1};
}

date date::operator+(const timedelta& delta) const {
  int y = year();
  int m = month();
  int d = day() + delta.days();
  if (normalize_date(&y, &m, &d) >= 0) {
    return date(y, m, d, detail::NonCheckTag{});
  }
  throw std::out_of_range("Date out of range after add op");
}

date& date::operator+=(const timedelta& delta) {
  *this = *this + delta;
  return *this;
}

date date::operator-(const timedelta& delta) const {
  int y = year();
  int m = month();
  int d = day() - delta.days();
  if (normalize_date(&y, &m, &d) >= 0) {
    return date(y, m, d, detail::NonCheckTag{});
  }
  throw std::out_of_range("Date out of range after sub op");
}

date& date::operator-=(const timedelta& delta) {
  *this = *this - delta;
  return *this;
}

timedelta date::operator-(const date& rhs) const {
  int lhs_ord = ymd_to_ord(year(), month(), day());
  int rhs_old = ymd_to_ord(rhs.year(), rhs.month(), rhs.day());
  return timedelta(lhs_ord - rhs_old, 0, 0, detail::NonNormTag{});
}

datetime datetime::strptime(const std::string& str, const std::string& fmt) {
  const char* pfmt = fmt.c_str();
  const char* pstr = str.c_str();

  int _year = 0;
  int _month = 0;
  int _day = 0;
  int _hour = 0;
  int _minute = 0;
  int _second = 0;
  int _microsecond = 0;

#define PARSE_02d(name)                                                          \
  if (pstr[1] == 0 || !isdigit(pstr[0]) || !isdigit(pstr[1])) {                  \
    goto error;                                                                  \
  }                                                                              \
  name = static_cast<int>(pstr[0] - '0') * 10 + static_cast<int>(pstr[1] - '0'); \
  pstr += 2;                                                                     \
  break;

  while (*pfmt && *pstr) {
    if (*pfmt != '%') {
      if (*pfmt != *pstr) {
        goto error;
      }
      ++pfmt;
      ++pstr;
      continue;
    }

    if (*pstr == 0) {
      goto error;
    }

    ++pfmt;
    // 如果到了末尾则*pfmt == '\0'
    switch (*pfmt) {
      case 'Y': {
        if (pstr[1] == 0 || pstr[2] == 0 || pstr[3] == 0 || !isdigit(pstr[0]) ||
            !isdigit(pstr[1]) || !isdigit(pstr[2]) || !isdigit(pstr[3])) {
          goto error;
        }
        _year = static_cast<int>(pstr[0] - '0') * 1000 + static_cast<int>(pstr[1] - '0') * 100 +
                static_cast<int>(pstr[2] - '0') * 10 + static_cast<int>(pstr[3] - '0');
        pstr += 4;
        break;
      }
      case 'm': {
        PARSE_02d(_month);
      }
      case 'd': {
        PARSE_02d(_day);
      }
      case 'H': {
        PARSE_02d(_hour);
      }
      case 'M': {
        PARSE_02d(_minute);
      }
      case 'S': {
        PARSE_02d(_second);
      }
      case 'f': {
        if (pstr[1] == 0 || pstr[2] == 0 || pstr[3] == 0 || pstr[4] == 0 || pstr[5] == 0 ||
            !isdigit(pstr[0]) || !isdigit(pstr[1]) || !isdigit(pstr[2]) || !isdigit(pstr[3]) ||
            !isdigit(pstr[4]) || !isdigit(pstr[5])) {
          goto error;
        }
        _microsecond =
            static_cast<int>(pstr[0] - '0') * 100000 + static_cast<int>(pstr[1] - '0') * 10000 +
            static_cast<int>(pstr[2] - '0') * 1000 + static_cast<int>(pstr[3] - '0') * 100 +
            static_cast<int>(pstr[4] - '0') * 10 + static_cast<int>(pstr[5] - '0');
        pstr += 6;
        break;
      }
      case '%': {
        if (*pstr != '%') {
          goto error;
        }
        ++pstr;
        break;
      }
      default: {
        goto error;
      }
    }

    ++pfmt;
  }

  if (*pfmt != 0) {
    goto error;
  }
  return datetime(_year, _month, _day, _hour, _minute, _second, _microsecond);

error:
  throw std::invalid_argument(
      fmt::format("datetime::strptime: Invalid format: date_string:{} fmt:{}", str, fmt));

#undef PARSE_02d
}

std::string date::strftime(const std::string& fmt) const {
  datetime dt(year(), month(), day(), 0, 0, 0, 0);
  return dt.strftime(fmt);
}

std::string date::ctime() const { return format_ctime(year(), month(), day(), 0, 0, 0); }

std::string date::isoformat() const {
  return fmt::format("{:04d}-{:02d}-{:02d}", year(), month(), day());
}

std::string date::str() const { return isoformat(); }

std::string date::repr() const { return fmt::format("date({}, {}, {})", year(), month(), day()); }

time::time() { std::memset(data_, 0, sizeof(data_)); }

time::time(int hour, int minute, int second, int usecond) {
  check_time_args(hour, minute, second, usecond);

  set_hour(hour);
  set_minute(minute);
  set_second(second);
  set_microsecond(usecond);
}

time::time(int hour, int minute, int second, int usecond, detail::NonCheckTag) {
  set_hour(hour);
  set_minute(minute);
  set_second(second);
  set_microsecond(usecond);
}

time time::fromisoformat(const std::string& time_string) {
  int hour = 0, minute = 0, second = 0, microsecond = 0;
  int tzoffset, tzimicrosecond = 0;
  // todo: 目前忽略了时区
  int rv = parse_isoformat_time(time_string.c_str(), time_string.size(), &hour, &minute, &second,
                                &microsecond, &tzoffset, &tzimicrosecond);
  if (rv < 0) {
    throw std::invalid_argument(
        fmt::format("time::fromisoformat: Invalid isoformat string: {}", time_string));
  }

  return time(hour, minute, second, microsecond);
}

std::string time::strftime(const std::string& fmt) const {
  datetime dt(1900, 1, 1, hour(), minute(), second(), microsecond());
  return dt.strftime(fmt);
}

std::string time::isoformat() const {
  int us = microsecond();
  if (us != 0) {
    return fmt::format("{:02d}:{:02d}:{:02d}.{:06d}", hour(), minute(), second(), us);
  } else {
    return fmt::format("{:02d}:{:02d}:{:02d}", hour(), minute(), second());
  }
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

::datetime::datetime datetime::kDatetimeEpoch{1970, 1, 1, 0, 0, 0, 0};

datetime::datetime(int year, int month, int day, int hour, int minute, int second, int usecond) {
  check_date_args(year, month, day);
  check_time_args(hour, minute, second, usecond);

  set_year(year);
  set_month(month);
  set_day(day);
  set_hour(hour);
  set_minute(minute);
  set_second(second);
  set_microsecond(usecond);
}

datetime::datetime(int year, int month, int day, int hour, int minute, int second, int usecond,
                   detail::NonCheckTag) {
  set_year(year);
  set_month(month);
  set_day(day);
  set_hour(hour);
  set_minute(minute);
  set_second(second);
  set_microsecond(usecond);
}

datetime::datetime(const datetime& other) { memcpy(data_, other.data_, sizeof(data_)); }

datetime datetime::now() {
  auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  return fromtimestamp(ts);
}

datetime datetime::fromtimestamp(std::chrono::microseconds timestamp) {
  struct tm tm;

  time_t ts_sec = timestamp.count() / 1000000UL;
  localtime_r(&ts_sec, &tm);
  if (tm.tm_sec > 59) {
    tm.tm_sec = 59;
  }

  return datetime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  timestamp.count() % 1000000UL);
}

datetime datetime::fromordinal(int ordinal) {
  if (ordinal < 1) {
    throw std::invalid_argument(fmt::format("datetime::fromordinal: Invalid ordinal: {}", ordinal));
  }

  int y;
  int m;
  int d;
  ord_to_ymd(ordinal, &y, &m, &d);
  return datetime(y, m, d);
}

datetime datetime::fromisocalendar(const IsoCalendarDate& iso_calendar) {
  auto d = ::datetime::date::fromisocalendar(iso_calendar);
  return datetime(d.year(), d.month(), d.day());
}

datetime datetime::combine(const ::datetime::date& d, const ::datetime::time& t) {
  return datetime(d.year(), d.month(), d.day(), t.hour(), t.minute(), t.second(), t.microsecond());
}

datetime datetime::operator+(const timedelta& delta) const {
  int y = year();
  int m = month();
  int d = day() + delta.days();
  int h = hour();
  int min = minute();
  int s = second() + delta.seconds();
  int us = microsecond() + delta.microseconds();

  if (normalize_datetime(&y, &m, &d, &h, &min, &s, &us) < 0) {
    throw std::out_of_range("Failed to normalize datetime");
  }
  return datetime(y, m, d, h, min, s, us);
}

datetime& datetime::operator+=(const timedelta& delta) {
  *this = *this + delta;
  return *this;
}

datetime datetime::operator-(const timedelta& delta) const {
  int y = year();
  int m = month();
  int d = day() - delta.days();
  int h = hour();
  int min = minute();
  int s = second() - delta.seconds();
  int us = microsecond() - delta.microseconds();

  if (normalize_datetime(&y, &m, &d, &h, &min, &s, &us) < 0) {
    throw std::out_of_range("Failed to normalize datetime");
  }
  return datetime(y, m, d, h, min, s, us);
}

datetime& datetime::operator-=(const timedelta& delta) {
  *this = *this - delta;
  return *this;
}

timedelta datetime::operator-(const datetime& rhs) const {
  auto delta_d =
      ymd_to_ord(year(), month(), day()) - ymd_to_ord(rhs.year(), rhs.month(), rhs.day());
  auto delta_s =
      (hour() - rhs.hour()) * 3600 + (minute() - rhs.minute()) * 60 + (second() - rhs.second());
  auto delta_us = microsecond() - rhs.microsecond();
  return timedelta(delta_d, delta_s, delta_us);
}

int datetime::weekday() const { return ::datetime::weekday(year(), month(), day()); }

int datetime::toordinal() const { return ymd_to_ord(year(), month(), day()); }

IsoCalendarDate datetime::isocalendar() const { return date().isocalendar(); }

std::chrono::microseconds datetime::timestamp() const {
  auto total_sec = local_to_seconds(year(), month(), day(), hour(), minute(), second(), 0);
  if (total_sec == -1) {
    throw std::out_of_range("Timestamp out of range");
  }
  total_sec -= EPOCH_SECONDS;
  return std::chrono::microseconds{total_sec * 1000000 + microsecond()};
}

std::string datetime::strftime(const std::string& fmt) const {
  std::stringstream ss;
  std::size_t i = 0;

  while (i < fmt.size()) {
    if (fmt[i] != '%') {
      ss << fmt[i];
      ++i;
      continue;
    }

    ++i;
    // 如果到了末尾则fmt[i] == '\0'
    switch (fmt[i]) {
      case 'a': {
        ss << kDayNames[weekday()];
        break;
      }
      case 'A': {
        ss << kDayFullNames[weekday()];
        break;
      }
      case 'w': {
        ss << ((weekday() + 1) % 7);
        break;
      }
      case 'd': {
        ss << std::setw(2) << std::setfill('0') << day();
        break;
      }
      case 'b': {
        ss << kMonthNames[month() - 1];
        break;
      }
      case 'B': {
        ss << kMonthFullNames[month() - 1];
        break;
      }
      case 'm': {
        ss << std::setw(2) << std::setfill('0') << month();
        break;
      }
      case 'y': {
        ss << std::setw(2) << std::setfill('0') << (year() % 100);
        break;
      }
      case 'Y': {
        ss << std::setw(4) << std::setfill('0') << year();
        break;
      }
      case 'H': {
        ss << std::setw(2) << std::setfill('0') << hour();
        break;
      }
      case 'I': {
        if (hour() == 0 || hour() == 12) {
          ss << "12";
        } else {
          ss << std::setw(2) << std::setfill('0') << (hour() % 12);
        }
        break;
      }
      case 'p': {
        if (hour() < 12) {
          ss << "AM";
        } else {
          ss << "PM";
        }
        break;
      }
      case 'M': {
        ss << std::setw(2) << std::setfill('0') << minute();
        break;
      }
      case 'S': {
        ss << std::setw(2) << std::setfill('0') << second();
        break;
      }
      case 'f': {
        ss << std::setw(6) << std::setfill('0') << microsecond();
        break;
      }
      case 'z': {
        // UTC offset
        // unsupported
        break;
      }
      case 'Z': {
        // time zone name. UTC, GMT
        // unsupported
        break;
      }
      case 'j': {
        ss << std::setw(3) << std::setfill('0') << (days_before_month(year(), month()) + day());
        break;
      }
      case 'U': {
        int first_weekday = ::datetime::weekday(year(), 1, 1);
        int first_sunday = 1;
        if (first_weekday < 6) {
          first_sunday += (6 - first_weekday);
        }
        int day_of_year = days_before_month(year(), month()) + day();
        if (day_of_year < first_sunday) {
          ss << "00";
        } else {
          ss << std::setw(2) << std::setfill('0') << (1 + (day_of_year - first_sunday) / 7);
        }
        break;
      }
      case 'W': {
        int first_weekday = ::datetime::weekday(year(), 1, 1);
        int first_monday = 1;
        if (first_weekday > 0) {
          first_monday += (6 - first_weekday + 1);
        }
        int day_of_year = days_before_month(year(), month()) + day();
        if (day_of_year < first_monday) {
          ss << "00";
        } else {
          ss << std::setw(2) << std::setfill('0') << (1 + (day_of_year - first_monday) / 7);
        }
        break;
      }
      case 'c': {
        ss << fmt::format("{} {} {:>2d} {:02d}:{:02d}:{:02d} {:04d}", kDayNames[weekday()],
                          kMonthNames[month() - 1], day(), hour(), minute(), second(), year());
        break;
      }
      case 'x': {
        ss << fmt::format("{:02d}/{:02d}/{:02d}", month(), day(), (year() % 100));
        break;
      }
      case 'X': {
        ss << fmt::format("{:02d}:{:02d}:{:02d}", hour(), minute(), second());
        break;
      }
      case '%': {
        ss << '%';
        break;
      }
      default: {
        throw std::invalid_argument(fmt::format("datetime::strftime: Invalid format: {}", fmt));
      }
    }

    ++i;
  }

  return ss.str();
}

std::string datetime::ctime() const {
  return format_ctime(year(), month(), day(), hour(), minute(), second());
}

std::string datetime::str() const {
  char sep = 'T';
  int us = microsecond();
  if (us != 0) {
    return fmt::format("{:04d}-{:02d}-{:02d}{}{:02d}:{:02d}:{:02d}.{:06d}", year(), month(), day(),
                       sep, hour(), minute(), second(), us);
  } else {
    return fmt::format("{:04d}-{:02d}-{:02d}{}{:02d}:{:02d}:{:02d}", year(), month(), day(), sep,
                       hour(), minute(), second());
  }
}

std::string datetime::repr() const {
  if (microsecond() != 0) {
    return fmt::format("datetime({}, {}, {}, {}, {}, {}, {})", year(), month(), day(), hour(),
                       minute(), second(), microsecond());
  } else if (second() != 0) {
    return fmt::format("datetime({}, {}, {}, {}, {}, {})", year(), month(), day(), hour(), minute(),
                       second());
  } else {
    return fmt::format("datetime({}, {}, {}, {}, {})", year(), month(), day(), hour(), minute());
  }
}

}  // namespace datetime
