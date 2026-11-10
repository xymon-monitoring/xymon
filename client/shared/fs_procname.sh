# fs_procname PID : the command name of a running process. ps is POSIX and
# reads process state only, so it cannot touch a wedged mount. Named so that
# df_sentinel() stays identical across the five clients, and so a test
# replaces the primitive rather than the code that uses it.
fs_procname()
{
	ps -o comm= -p "$1" 2>/dev/null | tr -d '[:space:]'
}
