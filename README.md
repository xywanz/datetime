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
// 目前如果指定的days, seconds, microseonds过大，标准化后可能会导致溢出且不抛出异常
// 这个问题会在后面的版本修复
timedelta(int days, int seconds = 0, int microseconds = 0);

// 支持更多时间单位，会遇到和上面构造方法同样的溢出问题
timedelta(int days, int seconds, int microseconds, int milliseconds, int minutes = 0, int hours = 0, int weeks = 0);

// timedelta(-MAX_DELTA_DAYS)
timedelta min();

// 返回所支持的最大时间间隔
timedelta max();

// 两个对象之间的最小时间间隔，即timedelta(0, 0, 1)
timedelta resolution();
```

timedelta的运算
```cpp
// 运算可能会导致溢出但不抛出异常，这个问题后续版本会解决
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
timedelta d1(2, 0, 0);
timedelta d2(1, 0, 0);

assert(d1 != d2);
assert((d1 == d2) == false);
assert(d1 > d2);
assert(d1 >= d2);
assert(d2 < d1);
assert(d2 <= d1);

// operator bool
assert((!d1) == false);
```

其他函数
```cpp
// 返回间隔的总秒数，会丢弃微秒部分
long total_seconds() const;
```
