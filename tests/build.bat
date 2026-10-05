@echo off
rem ============================================================================
rem v_alloc test driver.
rem
rem Runs the mtest suite for the bump-allocator contract, the resize/realloc
rem API, and an assert-enabled probe for the strict-LIFO release check.
rem
rem mtest is an external dependency. Set MVERSELIBS to a checkout of the
rem mverse-libs repository (the directory that contains mtest\).
rem ============================================================================
setlocal
if "%MVERSELIBS%"=="" (
  echo v_alloc tests: MVERSELIBS is not set.
  echo   Set MVERSELIBS to a checkout of mverse-libs ^(the directory containing mtest^).
  exit /b 4
)
if not exist "%MVERSELIBS%\mtest\run_tests.bat" (
  echo v_alloc tests: %MVERSELIBS%\mtest\run_tests.bat was not found.
  echo   Set MVERSELIBS to a checkout of mverse-libs ^(the directory containing mtest^).
  exit /b 4
)
set "TST_DIR=%~dp0."
set "TST_EXTRA_SOURCES=..\v_alloc.c"
set "TST_EXAMPLES=lifo_violation:42"
call "%MVERSELIBS%\mtest\run_tests.bat" v_alloc
endlocal & exit /b %ERRORLEVEL%
