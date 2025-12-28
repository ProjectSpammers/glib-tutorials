_glib_tutorials_completions() {
    local cur
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 1 ]]; then
        local commands=$(glib-tutorials --list-commands 2>/dev/null)
        COMPREPLY=( $(compgen -W "${commands}" -- ${cur}) )
    fi
}

complete -F _glib_tutorials_completions glib-tutorials
