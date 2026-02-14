# PCRE Compatibility Migration Checklist

This branch keeps a temporary compatibility layer so code can run on both
PCRE1 and PCRE2 while legacy call sites are migrated.

## Fast status check

1. Build with normal defaults (legacy shims enabled):
   - `XYMON_ENABLE_PCRE_LEGACY_SHIMS=1` (default)
2. Build with shims disabled:
   - `XYMON_ENABLE_PCRE_LEGACY_SHIMS=0`
3. Fix any remaining direct `pcre_*` call sites reported by the compiler/linker.

## Rules for new code

- Use `pcre_pattern_t` and `pcre_match_data_t` types.
- Prefer compat helpers:
  - `pcre_compile_compat()`
  - `pcre_exec_compat()`
  - `pcre_free_compat()`
  - `pcre_copy_substring_compat()`
- For legacy-ovector style code, use:
  - `pcre_exec_capture()`
  - `pcre_copy_substring_ovector()`
  - `pcre_free_pattern()`

## Final removal steps

1. Keep CI green with `XYMON_ENABLE_PCRE_LEGACY_SHIMS=0`.
2. Delete macro remaps in `lib/pcre_compat.h`.
3. Remove legacy wrapper exports:
   - `pcre_*_legacy()`
4. Keep only compat APIs in `lib/pcre_compat.[ch]`.
