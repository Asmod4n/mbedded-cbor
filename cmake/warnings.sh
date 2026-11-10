# Prints the warning flags of the tree for one compiler, from the lists beside this file.
# usage: . cmake/warnings.sh; warnings_for <compiler>. WARNINGS_DIR names the directory of the lists (/cmake).
warnings_for() {
	warnings_dir=${WARNINGS_DIR:-/cmake}
	cat "$warnings_dir/warnings-always.txt" "$warnings_dir/warnings-optimized.txt"
	case $(basename "$1") in
	clang*) cat "$warnings_dir/warnings-clang.txt" ;;
	esac
}
