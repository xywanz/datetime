# datetime
A Python style datetime library of C++

# Usage
```cpp
#include <iostream>

#include "datetime.h"

int main() {
    auto now = datetime::datetime::now;
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
