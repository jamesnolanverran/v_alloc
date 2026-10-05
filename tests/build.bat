@echo off
rem ============================================================================
rem v_alloc package test driver.
rem
rem v_alloc is a standalone package, not part of mverse-libs, so it points the
rem shared mtest runner at this directory with TST_DIR. The suite covers the
rem bump-allocator contract plus an assert-abort probe for strict-LIFO release.
rem ============================================================================
setlocal
set "TST_DIR=%~dp0."
set "TST_EXTRA_SOURCES=..\v_alloc.c"
set "TST_EXAMPLES=lifo_violation:42"
call "%~dp0..\..\mverse-libs\mtest\run_tests.bat" v_alloc
endlocal & exit /b %ERRORLEVEL%
