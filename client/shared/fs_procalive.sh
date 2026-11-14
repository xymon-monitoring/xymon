# fs_procalive PID : true while PID is a process that still runs. kill -0
# alone answers for a zombie too -- a process that has exited and stays in
# the table until its parent reaps it -- and a df left behind by an earlier
# cycle is an orphan, reaped by PID 1. Where PID 1 is not an init, in a
# container for one, nothing reaps it: it stays a zombie for good, and read
# as a df still wedged it kept the mount unavailable for as long. ps reads
# process state only, so it cannot touch a wedged mount; without ps, kill -0
# decides, as it did.
fs_procalive()
{
	kill -0 "$1" 2>/dev/null || return 1
	case "$(ps -o stat= -p "$1" 2>/dev/null)" in
	  *Z*) return 1 ;;
	esac
	return 0
}
