## Build
```OLD /usr/bin/cmake --build /home/ubuntu/rls_mini/build --config Debug --target install -j 3```

Configure
```cmake -DCMAKE_TOOLCHAIN_FILE=~/toolchain-aarch64.cmake --config Debug -S ~/rls_mini/src -B ~/rls_mini/build```

Compile
```cmake --build ~/rls_mini/build --target install -j4```

In case of cmake errors, drop ~/rls_mini/build

| Название контрольной точки | Код |
| - | - |
| Управление | 0 |
| Выход АЦП | 1 |
| Выход ФД | 2 |
| Выход ФАП | 3 |
| Выход ОФ | 4 |
| Выход ЛОУ | 5 |
| Выход КН | 6 |
| Выход АД | 7 |
| Выход АПУ | 8 |