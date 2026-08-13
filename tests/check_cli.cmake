file(WRITE "${WORK_DIR}/cli-input.tex" "1+1")
file(WRITE "${WORK_DIR}/repl-input.txt"
  ":help\nx:=2\nx+1\n:definitions\n:precision 30\n:backend symbolic ginac\n:quit\n")
file(WRITE "${WORK_DIR}/repl-short-help.txt" ":h\n:quit\n")

function(run_cli expected_status expected_stdout expected_stderr)
  execute_process(
    COMMAND "${CLI}" ${ARGN}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT status EQUAL expected_status OR NOT stdout MATCHES "${expected_stdout}" OR NOT stderr MATCHES "${expected_stderr}")
    message(FATAL_ERROR "CLI check failed: status=${status}, stdout=${stdout}, stderr=${stderr}")
  endif()
endfunction()

run_cli(0 "2" "^$" "1+1")
run_cli(0 "Usage: texsolve" "^$" --help)
run_cli(0 "Usage: texsolve" "^$" -h)
run_cli(0 "texsolve 0\\.1\\.0" "^$" --version)
run_cli(0 "texsolve 0\\.1\\.0" "^$" -v)
run_cli(0 "2" "^$" --file "${WORK_DIR}/cli-input.tex")
run_cli(0 "2" "^$" -f "${WORK_DIR}/cli-input.tex")
run_cli(0 "2" "Binary" --debug --file "${WORK_DIR}/cli-input.tex")
run_cli(0 "2" "Binary" -d -f "${WORK_DIR}/cli-input.tex")
run_cli(0 "2" "^$" differentiate "x^2")
run_cli(0 ".+" "^$" -- "-sin(x)")
run_cli(0 "-x" "^$" -- -x)
run_cli(0 "roots" "^$" solve "x^2=4")
run_cli(0 "domain: n" "^$" solve "\\sin{x}=0")
run_cli(0 "domain: x" "^$" solve "\\prod_{i=1}^{x}i=6")
run_cli(0 "matrix" "^$" linear [[\begin{pmatrix}1&2\\3&4\end{pmatrix}]])
run_cli(3 "^$" ".+" "\\unknown{x}")
run_cli(2 "^$" ".+" --unknown)
run_cli(2 "^$" ".+" -x)
run_cli(2 "^$" ".+" --repl solve)
run_cli(2 "^$" ".+" --debug --repl)

execute_process(
  COMMAND "${CLI}" --repl
  INPUT_FILE "${WORK_DIR}/repl-input.txt"
  RESULT_VARIABLE repl_status
  OUTPUT_VARIABLE repl_stdout
  ERROR_VARIABLE repl_stderr)
if(NOT repl_status EQUAL 0 OR NOT repl_stdout MATCHES "REPL commands:" OR
   NOT repl_stdout MATCHES "3" OR NOT repl_stdout MATCHES "1 variables")
  message(FATAL_ERROR "REPL check failed: status=${repl_status}, stdout=${repl_stdout}, stderr=${repl_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" -r
  INPUT_FILE "${WORK_DIR}/repl-short-help.txt"
  RESULT_VARIABLE short_repl_status
  OUTPUT_VARIABLE short_repl_stdout
  ERROR_VARIABLE short_repl_stderr)
if(NOT short_repl_status EQUAL 0 OR NOT short_repl_stdout MATCHES "REPL commands:")
  message(FATAL_ERROR "short REPL check failed: status=${short_repl_status}, stdout=${short_repl_stdout}, stderr=${short_repl_stderr}")
endif()

execute_process(
  COMMAND "${CLI}"
  INPUT_FILE "${WORK_DIR}/cli-input.tex"
  RESULT_VARIABLE stdin_status
  OUTPUT_VARIABLE stdin_stdout
  ERROR_VARIABLE stdin_stderr)
if(NOT stdin_status EQUAL 0 OR NOT stdin_stdout MATCHES "2" OR NOT stdin_stderr STREQUAL "")
  message(FATAL_ERROR "stdin check failed: status=${stdin_status}, stdout=${stdin_stdout}, stderr=${stdin_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" --file "${WORK_DIR}/cli-input.tex"
  INPUT_FILE "${WORK_DIR}/cli-input.tex"
  RESULT_VARIABLE conflict_status)
if(NOT conflict_status EQUAL 2)
  message(FATAL_ERROR "file/stdin conflict returned ${conflict_status}, expected 2")
endif()
