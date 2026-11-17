#!/bin/bash
set -euo pipefail
base=$1
head=$2
stripped() {
	git show "$1:$2" | "${CXX:-g++}" -fpreprocessed -dD -E -P -w -x c++ - | tr -s ' \t\n' ' '
}
while IFS= read -r path; do
	case "$path" in
	include/*.hpp | include/*.h | test/*.hpp | test/*.cpp | test/*.h | test/*.c)
		if ! git cat-file -e "$base:$path" 2>/dev/null || ! git cat-file -e "$head:$path" 2>/dev/null; then
			echo "$path"
		else
			old=$(stripped "$base" "$path")
			new=$(stripped "$head" "$path")
			if [ "$old" != "$new" ]; then
				echo "$path"
			fi
		fi
		;;
	include/* | test/* | cmake/* | CMakeLists.txt | third_party/* | third_party | test-vectors/* | cbor-test-vectors/* | .github/workflows/code.yml | .github/workflows/macos.yml | .github/workflows/windows.yml | .github/code_changed.sh)
		echo "$path"
		;;
	esac
done < <(git diff --name-only --no-renames "$base" "$head")
