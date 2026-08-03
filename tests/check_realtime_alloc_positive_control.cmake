if(NOT DEFINED REPORT OR REPORT STREQUAL "")
  message(FATAL_ERROR "REPORT must name the realtime audit JSON file")
endif()
if(NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "realtime audit report does not exist: ${REPORT}")
endif()

file(READ "${REPORT}" report_json)

function(read_json output)
  string(JSON value ERROR_VARIABLE json_error GET "${report_json}" ${ARGN})
  if(NOT json_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR
      "could not read JSON path '${ARGN}' from ${REPORT}: ${json_error}")
  endif()
  set(${output} "${value}" PARENT_SCOPE)
endfunction()

read_json(schema schema)
if(NOT schema STREQUAL "org.s3g.s3g-dsp.clap-realtime-audit/v1")
  message(FATAL_ERROR "unexpected realtime audit schema: ${schema}")
endif()

read_json(plugin_id plugin id)
if(NOT plugin_id STREQUAL
    "org.s3g.test.realtime-alloc-positive-control")
  message(FATAL_ERROR "audit loaded the wrong positive-control CLAP")
endif()

read_json(probe_enabled configuration allocation_probe)
if(NOT probe_enabled)
  message(FATAL_ERROR "audit report says the allocation probe was disabled")
endif()

string(JSON scenario_count ERROR_VARIABLE scenario_error LENGTH
  "${report_json}" scenarios)
if(NOT scenario_error STREQUAL "NOTFOUND" OR NOT scenario_count EQUAL 2)
  message(FATAL_ERROR
    "positive control must exercise baseline and automation scenarios")
endif()
math(EXPR last_scenario "${scenario_count} - 1")
set(saw_baseline FALSE)
set(saw_automation FALSE)

foreach(index RANGE 0 ${last_scenario})
  read_json(name scenarios ${index} name)
  if(name STREQUAL "baseline")
    set(saw_baseline TRUE)
  elseif(name STREQUAL "automation-burst")
    set(saw_automation TRUE)
  else()
    message(FATAL_ERROR "unexpected positive-control scenario '${name}'")
  endif()
  read_json(measured_iterations scenarios ${index} measured_iterations)
  read_json(measured_blocks scenarios ${index} allocation_probe measured_blocks)
  read_json(operation_blocks scenarios ${index} allocation_probe
    blocks_with_operations)
  read_json(allocation_blocks scenarios ${index} allocation_probe
    blocks_with_allocations)
  read_json(deallocation_blocks scenarios ${index} allocation_probe
    blocks_with_deallocations)
  read_json(allocation_calls scenarios ${index} allocation_probe totals
    allocation_calls)
  read_json(deallocation_calls scenarios ${index} allocation_probe totals
    deallocation_calls)
  read_json(requested_bytes scenarios ${index} allocation_probe totals
    requested_bytes)
  read_json(allocation_failures scenarios ${index} allocation_probe totals
    allocation_failures)
  read_json(invalid_alignments scenarios ${index} allocation_probe totals
    invalid_alignment_calls)
  read_json(max_operations scenarios ${index} allocation_probe max_per_block
    operations)
  read_json(max_allocations scenarios ${index} allocation_probe max_per_block
    allocation_calls)
  read_json(max_deallocations scenarios ${index} allocation_probe
    max_per_block deallocation_calls)
  read_json(max_requested_bytes scenarios ${index} allocation_probe
    max_per_block requested_bytes)
  read_json(malloc_calls scenarios ${index} allocation_probe calls_by_api
    malloc)
  read_json(free_calls scenarios ${index} allocation_probe calls_by_api free)
  read_json(finite_output scenarios ${index} finite_output)

  if(measured_iterations LESS 1
      OR NOT measured_blocks EQUAL measured_iterations
      OR NOT operation_blocks EQUAL measured_iterations
      OR NOT allocation_blocks EQUAL measured_iterations
      OR NOT deallocation_blocks EQUAL measured_iterations)
    message(FATAL_ERROR
      "scenario '${name}' did not report allocator activity in every "
      "measured block (${operation_blocks}/${measured_iterations})")
  endif()

  math(EXPR expected_requested_bytes "${measured_iterations} * 37")
  if(NOT allocation_calls EQUAL measured_iterations
      OR NOT deallocation_calls EQUAL measured_iterations
      OR NOT malloc_calls EQUAL measured_iterations
      OR NOT free_calls EQUAL measured_iterations
      OR NOT requested_bytes EQUAL expected_requested_bytes
      OR NOT max_operations EQUAL 2
      OR NOT max_allocations EQUAL 1
      OR NOT max_deallocations EQUAL 1
      OR NOT max_requested_bytes EQUAL 37
      OR NOT allocation_failures EQUAL 0
      OR NOT invalid_alignments EQUAL 0)
    message(FATAL_ERROR
      "scenario '${name}' did not preserve the expected one malloc/free "
      "positive-control contract")
  endif()
  if(NOT finite_output)
    message(FATAL_ERROR "scenario '${name}' produced non-finite output")
  endif()
endforeach()

if(NOT saw_baseline OR NOT saw_automation)
  message(FATAL_ERROR
    "positive control did not exercise both expected audit scenarios")
endif()

message(STATUS
  "Realtime allocation audit positive control passed (${scenario_count} scenarios)")
