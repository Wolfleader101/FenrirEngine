# The move-asset smoke run (cmake -P): copies the fixture root into WORK, then runs the tool on the copy.
#
#   GAME_TOOL    the test package's copy of the tool, which scans the fixtures' types
#   ENGINE_TOOL  fenrir-move-asset itself, with the engine's registrations alone
#   FIXTURE      the fixture root (tests/root)
#   WORK         a directory of the build tree to copy it into; emptied first
#
# A dry run lists the changes and writes nothing; the real move moves the sword and rewrites each reference to it but
# the unscanned spear's; a move onto an existing file is refused, exits 1 and writes nothing; the engine's own copy lists
# the fixtures' files as unscanned; bad arguments exit 2 with the usage.

foreach(variable GAME_TOOL ENGINE_TOOL FIXTURE WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "SmokeRun.cmake needs -D${variable}=...")
    endif()
endforeach()

set(from "weapons/sword.asset.json5")
set(to "arms/blade.asset.json5")
set(files "weapons/sword.asset.json5" "weapons/axe.asset.json5" "shields/shield.asset.json5" "spear.asset.json5")

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${FIXTURE}/" DESTINATION "${WORK}")

function(fail message)
    message(FATAL_ERROR "move-asset smoke run: ${message}")
endfunction()

# Runs the tool, setting code, out and err in the caller.
function(run tool)
    execute_process(COMMAND "${tool}" ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(code "${result}" PARENT_SCOPE)
    set(out "${output}" PARENT_SCOPE)
    set(err "${error}" PARENT_SCOPE)
endfunction()

# Fails unless every fixture file in the copy holds its fixture's bytes.
function(expect_untouched when)
    foreach(file IN LISTS files)
        if(NOT EXISTS "${WORK}/${file}")
            fail("${when}: ${file} is gone")
        endif()
        file(READ "${FIXTURE}/${file}" expected)
        file(READ "${WORK}/${file}" actual)
        if(NOT actual STREQUAL expected)
            fail("${when}: ${file} changed:\n${actual}")
        endif()
    endforeach()
    if(EXISTS "${WORK}/${to}")
        fail("${when}: ${to} was written")
    endif()
endfunction()

set(changed_lines "changed: arms/blade.asset.json5\nchanged: shields/shield.asset.json5\nchanged: weapons/axe.asset.json5\n")
set(unscanned_line "unscanned: spear.asset.json5: the type registry has no `AssetsTest::Spear`")

# The dry run: what would change, and nothing written.
run("${GAME_TOOL}" "${WORK}" "${from}" "${to}" --dry-run)
if(NOT code EQUAL 0)
    fail("the dry run exited ${code}:\n${out}${err}")
endif()
set(expected_dry "would move ${from} to ${to}, rewriting 3 references in 3 files\n${changed_lines}${unscanned_line}")
string(FIND "${out}" "${expected_dry}" at)
if(NOT at EQUAL 0)
    fail("the dry run printed\n${out}\nnot\n${expected_dry}")
endif()
expect_untouched("after the dry run")

# A move onto an existing file: refused with its reason, exit 1, nothing written. The engine's own copy refuses too.
run("${ENGINE_TOOL}" "${WORK}" "${from}" "weapons/axe.asset.json5")
if(NOT code EQUAL 1)
    fail("a move onto an existing file exited ${code}, not 1:\n${out}${err}")
endif()
if(NOT err MATCHES "^refused: .+")
    fail("a move onto an existing file gave no reason:\n${err}")
endif()
expect_untouched("after the refused move")

# The engine's own copy knows none of the fixtures' types, so scans none of them; the moved file is listed by its path
# after the move.
run("${ENGINE_TOOL}" "${WORK}" "${from}" "${to}" --dry-run)
if(NOT code EQUAL 0)
    fail("the engine's dry run exited ${code}:\n${out}${err}")
endif()
foreach(file "${to}" "weapons/axe.asset.json5" "shields/shield.asset.json5" "spear.asset.json5")
    string(FIND "${out}" "unscanned: ${file}: " at)
    if(at EQUAL -1)
        fail("the engine's dry run did not list ${file} as unscanned:\n${out}")
    endif()
endforeach()
expect_untouched("after the engine's dry run")

# Arguments it cannot read: the usage, exit 2.
run("${ENGINE_TOOL}" "${WORK}" "${from}")
if(NOT code EQUAL 2 OR NOT err MATCHES "^usage: move-asset <root> <from> <to> \\[--dry-run\\]")
    fail("two arguments exited ${code}, printing:\n${err}")
endif()

# The real move: the same report, the sword moved, each reference rewritten and every other byte kept.
run("${GAME_TOOL}" "${WORK}" "${from}" "${to}")
if(NOT code EQUAL 0)
    fail("the move exited ${code}:\n${out}${err}")
endif()
set(expected_move "moved ${from} to ${to}, rewriting 3 references in 3 files\n${changed_lines}${unscanned_line}")
string(FIND "${out}" "${expected_move}" at)
if(NOT at EQUAL 0)
    fail("the move printed\n${out}\nnot\n${expected_move}")
endif()
if(EXISTS "${WORK}/${from}")
    fail("${from} is still there after the move")
endif()
foreach(pair "weapons/sword.asset.json5|${to}" "weapons/axe.asset.json5|weapons/axe.asset.json5"
             "shields/shield.asset.json5|shields/shield.asset.json5")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 before)
    list(GET pair 1 after)
    file(READ "${FIXTURE}/${before}" expected)
    string(REPLACE "${from}" "${to}" expected "${expected}")
    if(NOT EXISTS "${WORK}/${after}")
        fail("${after} is missing after the move")
    endif()
    file(READ "${WORK}/${after}" actual)
    if(NOT actual STREQUAL expected)
        fail("${after} after the move is\n${actual}\nnot\n${expected}")
    endif()
endforeach()
# The spear could not be scanned, so it still names the old path.
file(READ "${FIXTURE}/spear.asset.json5" expected)
file(READ "${WORK}/spear.asset.json5" actual)
if(NOT actual STREQUAL expected)
    fail("the unscanned spear changed:\n${actual}")
endif()
