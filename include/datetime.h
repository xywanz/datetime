#pragma once

#include <cassert>
#include <chrono>
#include <compare>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace datetime {

constexpr int kMinYear = 1;
constexpr int kMaxYear = 9999;
constexpr int kMaxOrdinal = 3652059; /* date(9999,12,31).toordinal() */
constexpr int kMaxDeltaDays = 999999999;

namespace detail {
struct NonCheckTag {};
struct NonNormTag {};
struct NonNormNonCheckTag {};
}  // namespace detail

struct IsoCalendarDate {
  int year;
  int week;
  int weekday;
};

class timedelta {
 public:
  timedelta() {}
  explicit timedelta(int days, int seconds = 0, int microseconds = 0);
  timedelta(int days, int seconds, int microseconds, int milliseconds, int minutes = 0,
            int hours = 0, int weeks = 0);
  timedelta(const timedelta& other);

  timedelta(std::chrono::weeks weeks);
  timedelta(std::chrono::days days);
  timedelta(std::chrono::hours hours);
  timedelta(std::chrono::minutes minutes);
  timedelta(std::chrono::seconds seconds);
  timedelta(std::chrono::milliseconds milliseconds);
  timedelta(std::chrono::microseconds microseconds);

  static timedelta min() { return timedelta(-kMaxDeltaDays); }
  static timedelta max() { return timedelta(kMaxDeltaDays, 59, 99999, 0, 59, 23, 0); }
  static timedelta resolution() { return timedelta(0, 0, 1); }

  long total_seconds() const;
  long total_milliseconds() const;
  long total_microseconds() const { return delta_to_microseconds(); }

  operator bool() const { return days_ != 0 || seconds_ != 0 || microseconds_ != 0; }

  timedelta& operator=(const timedelta& rhs);
  timedelta operator+(const timedelta& rhs) const;
  timedelta& operator+=(const timedelta& rhs);
  timedelta operator-(const timedelta& rhs) const;
  timedelta& operator-=(const timedelta& rhs);
  timedelta operator/(const timedelta& rhs) const;
  timedelta& operator/=(const timedelta& rhs);
  timedelta operator%(const timedelta& rhs) const;
  timedelta& operator%=(const timedelta& rhs);

  std::strong_ordering operator<=>(const timedelta& rhs) const = default;

  timedelta operator+() const;
  timedelta operator-() const;

  timedelta operator*(int n) const;
  timedelta& operator*=(int n);
  timedelta operator/(int n) const;
  timedelta& operator/=(int n);
  friend timedelta operator*(int lhs, const timedelta& rhs);

  int days() const { return days_; }
  int seconds() const { return seconds_; }
  int microseconds() const { return microseconds_; }

  timedelta abs() const;
  std::string str() const;
  std::string repr() const;

 private:
  timedelta(int days, int seconds, int microseconds, detail::NonNormTag);
  timedelta(int days, int seconds, int microseconds, detail::NonNormNonCheckTag);

  static timedelta microseconds_to_delta(long us);
  long delta_to_microseconds() const;

  void frommicroseconds(long us);

  friend class date;
  friend class std::hash<timedelta>;

  int days_ = 0;
  int seconds_ = 0;
  int microseconds_ = 0;
};

class date {
 public:
  date(int year, int month, int day);

  static date today();
  static date fromisoformat(const std::string& date_string);
  static date fromtimestamp(std::chrono::microseconds timestamp);
  static date fromtimestamp(std::chrono::seconds timestamp);
  static date fromordinal(int ordinal);
  static date fromisocalendar(const IsoCalendarDate& iso_calendar);

  static date min() { return date(kMinYear, 1, 1, detail::NonCheckTag{}); }
  static date max() { return date(kMaxYear, 12, 31, detail::NonCheckTag{}); }

  static timedelta resolution() { return timedelta(1); }

  int year() const { return (static_cast<int>(data_[0]) << 8) | static_cast<int>(data_[1]); }
  int month() const { return static_cast<int>(data_[2]); }
  int day() const { return static_cast<int>(data_[3]); }
  int weekday() const;
  int isoweekday() const { return weekday() + 1; }
  int toordinal() const;
  IsoCalendarDate isocalendar() const;

  std::strong_ordering operator<=>(const date& rhs) const = default;

  date operator+(const timedelta& delta) const;
  date& operator+=(const timedelta& delta);
  date operator-(const timedelta& delta) const;
  date& operator-=(const timedelta& delta);
  timedelta operator-(const date& rhs) const;

  std::string strftime(const std::string& format) const;

  std::string ctime() const;
  std::string isoformat() const;

  std::string str() const;
  std::string repr() const;

 private:
  date(int year, int month, int day, detail::NonCheckTag);

  void set_year(int year) {
    data_[0] = static_cast<unsigned char>((year & 0xff00) >> 8);
    data_[1] = static_cast<unsigned char>(year & 0x00ff);
  }
  void set_month(int month) { data_[2] = static_cast<unsigned char>(month); }
  void set_day(int day) { data_[3] = static_cast<unsigned char>(day); }

  void set_fileds(int year, int month, int day) {
    set_year(year);
    set_month(month);
    set_day(day);
  }

  friend class datetime;
  friend class std::hash<date>;

  static constexpr int kDataSize = 4;
  unsigned char data_[kDataSize];
};

class time {
 public:
  /**
   * @brief 构造表示00:00:00的time
   */
  time();

  explicit time(int hour, int minute = 0, int second = 0, int usecond = 0);

  /**
   * @brief 从iso格式构造time
   * 不支持时区，如果有则会忽略。
   * 支持的格式：
   *   HH:MM:SS
   *   HH:MM
   *   HH
   * @param time_string
   * @return time
   * @throw std::invalid_exception 无法解析的格式
   */
  static time fromisoformat(const std::string& time_string);

  static time min() { return time(0, 0, 0, 0, detail::NonCheckTag{}); }
  static time max() { return time(23, 59, 59, 999999, detail::NonCheckTag{}); }

  static timedelta resolution() { return timedelta(0, 0, 1); }

  int hour() const { return static_cast<int>(data_[0]); }
  int minute() const { return static_cast<int>(data_[1]); }
  int second() const { return static_cast<int>(data_[2]); }
  int microsecond() const {
    return (static_cast<int>(data_[3]) << 16) | (static_cast<int>(data_[4]) << 8) |
           static_cast<int>(data_[5]);
  }

  operator bool() const {
    return *reinterpret_cast<const uint32_t*>(data_) != 0 ||
           *reinterpret_cast<const uint16_t*>(data_ + 4) != 0;
  }

  std::strong_ordering operator<=>(const time& rhs) const = default;

  std::string strftime(const std::string& format) const;

  /**
   * @brief 转换成HH:MM:SS格式的字符串
   *
   * @return std::string
   */
  std::string isoformat() const;

  std::string str() const;
  std::string repr() const;

 private:
  time(int hour, int minute, int second, int usecond, detail::NonCheckTag);

  void set_hour(int hour) { data_[0] = static_cast<unsigned char>(hour); }
  void set_minute(int minute) { data_[1] = static_cast<unsigned char>(minute); }
  void set_second(int second) { data_[2] = static_cast<unsigned char>(second); }
  void set_microsecond(int microsecond) {
    data_[3] = static_cast<unsigned char>((microsecond & 0xff0000) >> 16);
    data_[4] = static_cast<unsigned char>((microsecond & 0x00ff00) >> 8);
    data_[5] = static_cast<unsigned char>((microsecond & 0x0000ff));
  }

  friend class datetime;
  friend class std::hash<time>;

  static constexpr int kDataSize = 6;
  unsigned char data_[kDataSize];
};

class datetime {
 public:
  datetime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0,
           int usecond = 0);
  datetime(const datetime& other);

  static datetime now();

  /**
   * @brief 字符串转datetime
   * 支持部分python strptime格式化符号。
   * 将格式化的字符串转化为datetime，支持微秒级别的精度。
   * 格式化符号:
   *     %Y 四位数表示的年份 (0001-9999)
   *     %m 两位数表示的月份 (01-12)
   *     %d 两位数表示的月份中的一天 (01-31)
   *     %H 两位数表示的小时数 (00-23)
   *     %M 两位数表示的分钟数 (00-59)
   *     %S 两位数表示的秒钟数 (00-59)
   *     %f 六位数表示的微秒数 (000000-999999)
   *     %% 百分号
   * 示例：
   *    auto dt = datetime::strptime("2021/08/31 15:59:55.123456", "%Y/%m/%d %H:%M:%S.%f");
   *    assert(dt == datetime(2021, 8, 31, 15, 59, 55, 123456));
   * @param date_string
   * @param format
   * @return datetime
   * @exception std::invalid_argument 解析失败
   */
  static datetime strptime(const std::string& date_string, const std::string& format);

  /**
   * @brief 从微秒时间戳创建datetime
   *
   * @param timestamp 微秒时间戳，即unix时间戳*1,000,000 + 微秒数
   * @return datetime
   */
  static datetime fromtimestamp(std::chrono::microseconds timestamp);
  static datetime fromordinal(int ordinal);
  static datetime fromisocalendar(const IsoCalendarDate& iso_calendar);
  static datetime combine(const ::datetime::date& d, const ::datetime::time& t);

  static datetime min() { return datetime(kMinYear, 1, 1, 0, 0, 0, 0, detail::NonCheckTag{}); }
  static datetime max() {
    return datetime(kMaxYear, 12, 31, 23, 59, 59, 999999, detail::NonCheckTag{});
  }

  static timedelta resolution() { return timedelta(0, 0, 1); }

  std::strong_ordering operator<=>(const datetime& rhs) const = default;

  datetime operator+(const timedelta& delta) const;
  datetime& operator+=(const timedelta& delta);
  datetime operator-(const timedelta& delta) const;
  datetime& operator-=(const timedelta& delta);

  timedelta operator-(const datetime& rhs) const;

  ::datetime::date date() const {
    return ::datetime::date(year(), month(), day(), detail::NonCheckTag{});
  }
  ::datetime::time time() const {
    return ::datetime::time(hour(), minute(), second(), microsecond(), detail::NonCheckTag{});
  }

  int year() const { return (static_cast<int>(data_[0]) << 8) | static_cast<int>(data_[1]); }
  int month() const { return static_cast<int>(data_[2]); }
  int day() const { return static_cast<int>(data_[3]); }
  int hour() const { return static_cast<int>(data_[4]); }
  int minute() const { return static_cast<int>(data_[5]); }
  int second() const { return static_cast<int>(data_[6]); }
  int microsecond() const {
    return (static_cast<int>(data_[7]) << 16) | (static_cast<int>(data_[8]) << 8) |
           static_cast<int>(data_[9]);
  }
  int weekday() const;
  int isoweekday() const { return weekday() + 1; }
  int toordinal() const;
  IsoCalendarDate isocalendar() const;

  std::chrono::microseconds timestamp() const;

  /**
   * @brief datetime转字符串
   * 和python的strftime格式化符号基本一致
   * 格式化符号：
   *     %a  Abbreviated weekday name. Sun, Mon, ...
   *     %A  Full weekday name. Sunday, Monday, ...
   *     %w  Weekday as a decimal number. 0, 1, ..., 6
   *     %d  Day of the month as a zero added decimal. 01, 02, ..., 31
   *     %b  Day of the month as a decimal number. Jan, Feb, ..., Dec
   *     %B  Full month name. January, February, ...
   *     %m  Month as a zero added decimal number. 01, 02, ..., 12
   *     %y  Year without century as a zero added decimal number. 00, 01, ..., 99
   *     %Y  Year with century as a decimal number. 2021, 2022 etc.
   *     %H  Hour (24-hour clock) as a zero added decimal number. 00, 01, ..., 23
   *     %I  Hour (12-hour clock) as a zero added decimal number. 01, 02, ..., 12
   *     %p  Locale’s AM or PM. AM, PM
   *     %M  Minute as a zero added decimal number. 00, 01, ..., 59
   *     %S  Second as a zero added decimal number. 00, 01, ..., 59
   *     %f  Microsecond as a decimal number, zero added on the left. 000000 - 999999
   *     %z  (Unsupported) UTC offset in the form +HHMM or -HHMM.
   *     %Z  (Unsupported) Time zone name.
   *     %j  Day of the year as a zero added decimal number. 001, 002, ..., 366
   *     %U  Week number of the year (Sunday as the first day of the week). All days in a new year
   *         preceding the first Sunday are considered to be in week 0. 00, 01, ..., 53
   *     %W  Week number of the year (Monday as the first day of the week). All days in a new year
   *         preceding the first Monday are considered to be in week 0. 00, 01, ..., 53
   * @param format
   * @return std::string
   * @exception std::invalid_argument conv failed
   */
  std::string strftime(const std::string& format) const;

  std::string ctime() const;

  std::string str() const;
  std::string repr() const;

 private:
  datetime(int year, int month, int day, int hour, int minute, int second, int usecond,
           detail::NonCheckTag);

  void set_year(int year) {
    data_[0] = static_cast<unsigned char>((year & 0xff00) >> 8);
    data_[1] = static_cast<unsigned char>(year & 0x00ff);
  }
  void set_month(int month) { data_[2] = static_cast<unsigned char>(month); }
  void set_day(int day) { data_[3] = static_cast<unsigned char>(day); }
  void set_hour(int hour) { data_[4] = static_cast<unsigned char>(hour); }
  void set_minute(int minute) { data_[5] = static_cast<unsigned char>(minute); }
  void set_second(int second) { data_[6] = static_cast<unsigned char>(second); }
  void set_microsecond(int microsecond) {
    data_[7] = static_cast<unsigned char>((microsecond & 0xff0000) >> 16);
    data_[8] = static_cast<unsigned char>((microsecond & 0x00ff00) >> 8);
    data_[9] = static_cast<unsigned char>((microsecond & 0x0000ff));
  }

  friend class std::hash<datetime>;

  static datetime kDatetimeEpoch;

  static constexpr int kDataSize = 10;
  unsigned char data_[kDataSize];
};

inline timedelta operator*(int lhs, const timedelta& rhs) { return rhs * lhs; }

}  // namespace datetime

namespace std {
template <>
struct hash<datetime::timedelta> {
  using argument_type = datetime::timedelta;
  using result_type = std::size_t;
  result_type operator()(const argument_type& delta) const {
    auto data = delta.delta_to_microseconds();
    return std::hash<decltype(data)>{}(data);
  }
};

template <>
struct hash<datetime::date> {
  using argument_type = datetime::date;
  using result_type = std::size_t;
  result_type operator()(const argument_type& d) const {
    std::string_view bytes((const char*)d.data_, d.kDataSize);
    return std::hash<std::string_view>{}(bytes);
  }
};

template <>
struct hash<datetime::time> {
  using argument_type = datetime::time;
  using result_type = std::size_t;
  result_type operator()(const argument_type& t) const {
    std::string_view bytes((const char*)t.data_, t.kDataSize);
    return std::hash<std::string_view>{}(bytes);
  }
};

template <>
struct hash<datetime::datetime> {
  using argument_type = datetime::datetime;
  using result_type = std::size_t;
  result_type operator()(const argument_type& dt) const noexcept {
    std::string_view bytes((const char*)dt.data_, dt.kDataSize);
    return std::hash<std::string_view>{}(bytes);
  }
};
}  // namespace std
