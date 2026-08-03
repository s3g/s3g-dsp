if(NOT DEFINED REPORT OR REPORT STREQUAL "")
  message(FATAL_ERROR "REPORT must name the Horizon realtime audit JSON")
endif()
if(NOT EXISTS "${REPORT}")
  message(FATAL_ERROR "Horizon realtime report does not exist: ${REPORT}")
endif()
if(NOT DEFINED REQUIRE_ZERO_ALLOC)
  set(REQUIRE_ZERO_ALLOC FALSE)
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
read_json(plugin_id plugin id)
if(NOT schema STREQUAL "org.s3g.s3g-dsp.clap-realtime-audit/v1"
    OR NOT plugin_id STREQUAL
      "org.s3g.s3g-dsp.ambi-horizon-encoder-64")
  message(FATAL_ERROR "unexpected Horizon realtime report identity")
endif()

string(JSON scenario_count ERROR_VARIABLE scenario_error LENGTH
  "${report_json}" scenarios)
if(NOT scenario_error STREQUAL "NOTFOUND" OR scenario_count LESS 1)
  message(FATAL_ERROR "Horizon realtime report contains no scenarios")
endif()
math(EXPR last_scenario "${scenario_count} - 1")
set(observed_deadline_misses 0)
set(maximum_p95_deadline_load 0.0)
set(maximum_observed_p99_deadline_load 0.0)

# An offline audit runs on a pre-emptible desktop thread, so a single block can
# be delayed by the OS even when the callback itself is comfortably realtime.
# Gate sustained p95 load instead.  Raw misses and p99 remain visible below.
set(maximum_allowed_p95_deadline_load 0.90)

foreach(index RANGE 0 ${last_scenario})
  read_json(name scenarios ${index} name)
  read_json(sample_rate scenarios ${index} sample_rate)
  read_json(block_size scenarios ${index} block_size)
  read_json(deadline_misses scenarios ${index} deadline_misses)
  read_json(p95_deadline_load scenarios ${index} deadline_load p95)
  read_json(p99_deadline_load scenarios ${index} deadline_load p99)
  read_json(finite_output scenarios ${index} finite_output)
  string(JSON error_type ERROR_VARIABLE type_error TYPE
    "${report_json}" scenarios ${index} error)
  if(NOT type_error STREQUAL "NOTFOUND" OR NOT error_type STREQUAL "NULL")
    message(FATAL_ERROR
      "Horizon scenario '${name}' ${sample_rate} Hz/${block_size} has an error")
  endif()
  if(NOT finite_output)
    message(FATAL_ERROR
      "Horizon scenario '${name}' ${sample_rate} Hz/${block_size} failed: "
      "finite=${finite_output}")
  endif()
  if(p95_deadline_load GREATER maximum_allowed_p95_deadline_load)
    message(FATAL_ERROR
      "Horizon scenario '${name}' ${sample_rate} Hz/${block_size} has "
      "sustained p95 callback load ${p95_deadline_load}, above "
      "${maximum_allowed_p95_deadline_load}; p99=${p99_deadline_load}, "
      "raw deadline misses="
      "${deadline_misses}")
  endif()
  if(p95_deadline_load GREATER maximum_p95_deadline_load)
    set(maximum_p95_deadline_load "${p95_deadline_load}")
  endif()
  if(p99_deadline_load GREATER maximum_observed_p99_deadline_load)
    set(maximum_observed_p99_deadline_load "${p99_deadline_load}")
  endif()
  math(EXPR observed_deadline_misses
    "${observed_deadline_misses} + ${deadline_misses}")

  if(REQUIRE_ZERO_ALLOC)
    read_json(operation_blocks scenarios ${index} allocation_probe
      blocks_with_operations)
    read_json(allocation_calls scenarios ${index} allocation_probe totals
      allocation_calls)
    read_json(deallocation_calls scenarios ${index} allocation_probe totals
      deallocation_calls)
    read_json(allocation_failures scenarios ${index} allocation_probe totals
      allocation_failures)
    if(NOT operation_blocks EQUAL 0
        OR NOT allocation_calls EQUAL 0
        OR NOT deallocation_calls EQUAL 0
        OR NOT allocation_failures EQUAL 0)
      message(FATAL_ERROR
        "Horizon scenario '${name}' ${sample_rate} Hz/${block_size} touched "
        "the allocator: blocks=${operation_blocks}, alloc=${allocation_calls}, "
        "free=${deallocation_calls}, failures=${allocation_failures}")
    endif()
  endif()
endforeach()

message(STATUS
  "Horizon realtime gate passed: ${scenario_count} scenarios, "
  "maximum p95 deadline load=${maximum_p95_deadline_load}, "
  "maximum observed p99=${maximum_observed_p99_deadline_load}, "
  "advisory raw deadline misses=${observed_deadline_misses}, "
  "zero-alloc=${REQUIRE_ZERO_ALLOC}")
