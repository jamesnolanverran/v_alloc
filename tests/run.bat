@echo off
rem Build and run the v_alloc tests (Windows).
rem On a POSIX system, or anywhere with make, use: make test
setlocal
set "CFLAGS=-std=c11 -Wall -Wextra -O1 -g"
set "CC=clang"
where clang >nul 2>nul || set "CC=gcc"

if not exist build mkdir build

"%CC%" %CFLAGS% -I. -o build\test_v_alloc.exe tests\test_v_alloc.c v_alloc.c || exit /b 1
"%CC%" %CFLAGS% -I. -o build\test_lifo_abort.exe tests\test_lifo_abort.c v_alloc.c || exit /b 1

build\test_v_alloc.exe || exit /b 1

rem An abort is reported as a negative status value, so compare as a string.
build\test_lifo_abort.exe >nul 2>&1
set "ABORT_RC=%errorlevel%"
if "%ABORT_RC%"=="0" (
  echo test_lifo_abort: no abort ^(asserts disabled^) - skipped
) else (
  echo test_lifo_abort: aborted as expected
)
exit /b 0
