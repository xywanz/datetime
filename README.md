# datetime
A Python style datetime library of C++

# Usage
```cpp
#include <iostream>

#include "datetime.h"

int main() {
    auto now = datetime::datetime::now();
    std::cout << now.str() << std::endl;
    now += datetime::timedelta(1);
    std::cout << now.str() << std::endl;
}
```

# Build
## add as subproject
```cmake
    add_subdirectory(datetime)

    add_executable(my_program main.cc)
    target_include_libraries(my_program datetime::datetime)
```

# timedelta
timedelta表示两个date或datetime的时间间隔

timedelta只会把days、seconds、microseconds存储在内部，它们都是int32_t类型，其他的时间单位将以如下规则转化成days、seconds以及microseconds
- 1ms -> 1000us
- 1 minute -> 60s
- 1 hour -> 3600s
- 1 week -> 7 days

为了使timedelta的表示方式唯一，timedelta会对days、seconds以及microseconds进行标准化，并保证它们的范围如下所示
- 0 <= microseonds <= 1000000
- 0 <= seconds <= 3600*24
- -999999999 <= days <= 999999999

下面的例子演示了如何对days, seconds 和 microseconds以外的任意参数执行“合并”操作并标准化为以上三个结果属性
```cpp
    int days = 50;
    int seconds = 27;
    int microseconds = 10;
    int milliseconds = 29000;
    int minutes = 5;
    int hours = 8;
    int weeks = 2;
    datetime::timedelta delta(days, seconds, microseconds, milliseconds, minutes, hours, weeks);
    assert(delta.days() == 64);
    assert(delta.seconds() == 29156);
    assert(delta.micrseconds() == 10);
```

timedelta的构造
```cpp
// 构造一个空的间隔，即days=0, seconds=0, micrseconds=0
timedelta();

// 指定days, seconds, microseconds
timedelta(int days, int seconds = 0, int microseconds = 0);

timedelta(int days, int seconds, int microseconds, int milliseconds, int minutes = 0, int hours = 0, int weeks = 0);

// timedelta(-MAX_DELTA_DAYS)
static timedelta min();

// 返回所支持的最大时间间隔
static timedelta max();

// 两个对象之间的最小时间间隔，即timedelta(0, 0, 1)
static timedelta resolution();
```

timedelta的运算
```cpp
timedelta d1;
timedelta d2;
int i;

d1 + d2;  // timedelta + timedelta
d1 - d2;  // timedelta - timedetla
d1 / d2;  // timedelta / timedelta
d1 % d2;  // timedelta % timedelta
i * d1;  // int * timedelta
d1 * i;  // timedelta * int
d1 / i;  // timedelta / i
+d1;  // +timedelta
-d1;  // -timedelta
d1.abs();  // abs(timedelta)
```

timedelta之间的比较
```cpp
timedelta d1{2, 0, 0};
timedelta d2{1, 0, 0};

assert(d1 != d2);
assert((d1 == d2) == false);
assert(d1 > d2);
assert(d1 >= d2);
assert(d2 < d1);
assert(d2 <= d1);

// operator bool
assert((!d1) == false);
```

timedelta的其他成员函数
```cpp
int days() const;

int seconds() const;

int microseconds() const;

// 返回间隔的总秒数，会丢弃微秒部分
long total_seconds() const;
```

# date
date 对象代表一个理想化历法中的日期（年、月和日），即当今的格列高利历向前后两个方向无限延伸。

公元1年1月1日是第1日，公元1年1月2日是第2日，依此类推。

date内部有三个属性，year、month、day，合法的年月日需满足以下条件
- MINYEAR <= year <= MAXYEAR
- 1 <= month <= 12
- 1 <= day <= 给定年月对应的天数


date的构造
```cpp
// 根据年月日构造，如果年月日不合法，则抛出异常
date(int year, int month, int day);

// 获取当天的date
static date today();

// date_string的格式必须为YYYY-MM-DD
static date fromisoformat(const std::string& date_string);

// 从时间戳构造，返回当地日期
static datetime fromtimestamp(std::chrono::seconds);
static datetime fromtimestamp(std::chrono::milliseconds);
static datetime fromtimestamp(std::chrono::microseconds);

// 根据格里高历序号来构造，即1代表公元1年1月1日，2代表公元1年1月2日，以此类推
static date fromordinal(int ordinal);

// date(MINYEAR, 1, 1)
static date max();

// date(MAXYEAR, 12, 31)
static date min();
```

date的运算
```cpp
date d1{2021, 1, 1};
date d2{2020, 1, 1};
timedelta delta{366};

d1 - d2;  // timedelta = date - date
d1 + delta;  // date + timedelta
d2 - delta;  // date - timedelta
```

date之间的比较
```cpp
date d1{2021, 1, 1};
date d2{2020, 1, 1};

assert(d1 != d2);
assert((d1 == d2) == false);
assert(d1 > d2);
assert(d1 >= d2);
assert(d2 < d1);
```

date的其他成员函数
```cpp
int year();

int month();

int day();

// 两个date之间的最小间隔，即timedelta(1)
timedelta resolution();
```

# time
一个time对象代表某日的（本地）时间，它独立于任何特定日期

time有以下属性
- 0 <= hour < 24
- 0 <= minute < 60
- 0 <= second < 60
- 0 <= microsecond < 1000000

time不支持算术运算以及比较运算

# datetime
datetime即包含了date的信息又包含了time的信息

与date一样，datetime假定当前的格列高利历向前后两个方向无限延伸；与time一样，datetime假定每一天恰好有3600*24秒

datetime支持从以下时间单位来构造，year、month、day参数是必须要填写的
- MINYEAR <= year <= MAXYEAR
- 1 <= month <= 12
- 1 <= day <= 指定年月的天数
- 0 <= hour < 24
- 0 <= minute < 60
- 0 <= second < 60
- 0 <= microsecond < 1000000

datetime的构造
```cpp
datetime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int usecond = 0);

static datetime now();

static datetime fromtimestamp(std::chrono::seconds);
static datetime fromtimestamp(std::chrono::milliseconds);
static datetime fromtimestamp(std::chrono::microseconds);

static datetime min()；

static datetime max()；

static datetime strptime(const std::string& date_string, const std::string& format);
```

datetime之间的运算
```cpp
datetime dt1{2021, 1, 1, 0, 30, 30, 1000};
datetime dt2{2020, 1, 1, 1, 30, 24, 8000};
timedelta delta{366};

dt1 - dt2;  // timedelta = datetime - datetime
dt1 + delta;  // datetime + timedelta
dt2 - delta;  // datetime - timedelta
```

datetime之间的比较
```cpp
datetime dt1{2021, 1, 1, 0, 30, 30, 1000};
datetime dt2{2020, 1, 1, 1, 30, 24, 8000};

assert(dt1 != dt2);
assert((dt1 == dt2) == false);
assert(dt1 > dt2);
assert(dt1 >= dt2);
assert(dt2 < dt1);
```

datetime的其他成员函数
```cpp
int year() const;
int month() const;
int day() const;
int hour() const;
int minute() const;
int second() const;
int microsecond() const;
::datetime::date date() const;
::datetime::time time() const;
std::string strftime(const std::string& format) const;
```
